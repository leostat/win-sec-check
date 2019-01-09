/*
 * sysinfo.c
 *
 * WinSecCheck.
 *
 * - thesnoom 2018
 */

#pragma warning(disable : 4731)
#pragma warning(disable : 4996)


#include <Windows.h>
#include <stdio.h>

#include "main.h"
#include "sysinfo.h"
#include "token.h"
#include "descriptors.h"


typedef LONG (WINAPI *RtlGetVer)(OSVERSIONINFOEXW *);
typedef LONG (WINAPI *NtQuerySystemInfo)(DWORD SystemInformationClass, VOID *SystemInformation, DWORD SystemInformationLength, DWORD *ReturnLength);

#define STATUS_SUCCESS 0x00000000
#define STATUS_INFO_LENGTH_MISMATCH ((LONG)0xC0000004L)


unsigned long dwDomainFlags;


// Detect HT and use GetSystemInfo to display phy/log cores.
void DisplayCoreInfo( void )
{
	int nHyper = 0, nArch = 0, corePhys, coreLog;
	char szCpuInfo[64] = { 0 };
	unsigned regs[4] = { 0 };

	/*__asm		// Doesn't work on x64... MSVC disabled inline x64 support. ;_;
	{
		pushad

		mov eax, 1
		cpuid

		test edx, 0x10000000
		jz Nope
		mov nHyper, 1

		Nope:
		popad
	}*/

	// https://social.msdn.microsoft.com/Forums/en-US/3a771604-5a21-4170-8177-9f37c71be81f/hyperthreading-with-intel-processors-with-x64-bit-vc-application?forum=vclanguage
	if((__cpuid(regs, 0), regs[0]) > 0)
	{
		__cpuid(regs, 1);

		nHyper = (regs[3] & ( 1 << 28 )) != 0;
	}

	// Query extended identifiers
	__cpuid(regs, 0x80000000);

	if(regs[0] >= 0x80000002) // 0x8000000[2-4] -> CPU String
	{
		__cpuid(regs, 0x80000002);
		memcpy(szCpuInfo, regs, sizeof(regs));

		__cpuid(regs, 0x80000003);
		memcpy(szCpuInfo + 16, regs, sizeof(regs));

		__cpuid(regs, 0x80000004);
		memcpy(szCpuInfo + 32, regs, sizeof(regs));
	}

	SYSTEM_INFO sysInfo = { 0 };
	GetNativeSystemInfo(&sysInfo);

	switch(sysInfo.wProcessorArchitecture)
	{
		case PROCESSOR_ARCHITECTURE_AMD64:
		{
			nArch = 64;
			break;
		}

		case PROCESSOR_ARCHITECTURE_INTEL:
		{
			nArch = 86;
			break;
		}
	}

	corePhys = coreLog = (int)sysInfo.dwNumberOfProcessors;

	if(nHyper)
		corePhys /= 2;

	char finalDisp[256] = { 0 };

	if(szCpuInfo[0] != '\0')
		snprintf(finalDisp, sizeof(finalDisp), "x%d - %dC(%dT) - %s\n", nArch, corePhys, coreLog, szCpuInfo);
	else
		snprintf(finalDisp, sizeof(finalDisp), "x%d - %dC(%dT)\n", nArch, corePhys, coreLog);

	printf("- %-20s %s", "CPU Info:", finalDisp);
	for(size_t i = 0; i < (22 + strlen(finalDisp)); printf("-"), i++); puts("");
}


// RtlGetVersion() :: https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/wdm/nf-wdm-rtlgetversion
// Since Microsoft broke GetVersionEx >= W8.1, this is the best method.
void DisplayWinVerInfo( void )
{
	OSVERSIONINFOEXW osVer	= { 0 };
	HMODULE hNtDLL			= NULL;
	RtlGetVer RtlGetVersion = NULL;

	hNtDLL = LoadLibraryA("ntdll.dll");
	if(!hNtDLL)
	{
		printf("[!] LoadLibrary (%d) :: Error getting handle to ntdll.\n", GetLastError());
		return;

	} else {
		RtlGetVersion = (RtlGetVer)GetProcAddress(hNtDLL, "RtlGetVersion");
		if(RtlGetVersion)
		{
			ZeroMemory(&osVer, sizeof(OSVERSIONINFOEXW));
			osVer.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

			if(RtlGetVersion(&osVer) == STATUS_SUCCESS)
			{
				char szServPack[128]	= { 0 };
				char szProdName[128]	= { 0 };
				char szProdVer[24]		= { 0 };
				char szNtType[24]		= { 0 };
				DWORD dwBuffSize		= 128;
				DWORD dwProdSize		= 24;

				ZeroMemory(szServPack, 128);
				ZeroMemory(szProdName, 128);
				ZeroMemory(szProdVer,	24);
				ZeroMemory(szNtType,	24);

				switch(osVer.wProductType)
				{
					case VER_NT_WORKSTATION:
					{
						memcpy(szNtType, "Desktop", strlen("Desktop"));
						break;
					}

					case VER_NT_SERVER:
					{
						memcpy(szNtType, "Server", strlen("Server"));
						break;
					}

					case VER_NT_DOMAIN_CONTROLLER:
					{
						memcpy(szNtType, "Domain Controller", strlen("Domain Controller"));
						dwDomainFlags |= WSC_DOMAINDC;

						break;
					}
				}

				if(osVer.szCSDVersion[0] == L'\0')
					memcpy(szServPack, "N/A", strlen("N/A"));
				else {
					WCHAR *src	= osVer.szCSDVersion;
					char *dst	= szServPack;

					while(*src)
						*dst++ = (char)*src++;
				}
				
				RegGetValue(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ProductName", RRF_RT_ANY, NULL, (void *)&szProdName, &dwBuffSize);
				RegGetValue(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", "ReleaseId", RRF_RT_ANY, NULL, (void *)&szProdVer, &dwProdSize);

				char finalDisp[256] = { 0 };
				if(szProdVer[0] != '\0')
					snprintf(finalDisp, sizeof(finalDisp), "%s %s - %d.%d.%d (%s)\n", szProdName, szProdVer, osVer.dwMajorVersion, osVer.dwMinorVersion, osVer.dwBuildNumber, szNtType);
				else
					snprintf(finalDisp, sizeof(finalDisp), "%s - %d.%d.%d (%s)\n", szProdName, osVer.dwMajorVersion, osVer.dwMinorVersion, osVer.dwBuildNumber, szNtType);

				printf("- %-20s %s", "System Version:", finalDisp);

				ZeroMemory(finalDisp, sizeof(finalDisp));
				snprintf(finalDisp, sizeof(finalDisp), "%d.%d (%s)\n", osVer.wServicePackMajor, osVer.wServicePackMinor, szServPack);

				printf("- %-20s %s", "Service Pack:", finalDisp);
			}

		} else
			printf("[!] GetProcAddress (%d) :: RtlGetVersion API address.\n", GetLastError());

		FreeLibrary(hNtDLL);
	}
}


// Utilise NtQuerySystemInformation to list process information
void DisplayProcesses( void )
{
	HMODULE hNtDLL = NULL;
	NtQuerySystemInfo NtQuerySystemInformation = NULL;

	hNtDLL = LoadLibraryA("ntdll.dll");
	if(!hNtDLL)
	{
		printf("[!] LoadLibrary (%d) :: Error getting handle to ntdll.\n", GetLastError());
		return;

	} else {
		NtQuerySystemInformation = (NtQuerySystemInfo)GetProcAddress(hNtDLL, "NtQuerySystemInformation");
		if(NtQuerySystemInformation)
		{
			SYSTEM_PROCESS_INFORMATION *pSysProcInf = NULL, *pSysProcHead = NULL;
			DWORD dwSysProcLen = (sizeof(SYSTEM_PROCESS_INFORMATION) * 128);
			LONG ntRet = 0x00;
			BOOL bMore = TRUE;

			pSysProcInf = (SYSTEM_PROCESS_INFORMATION *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSysProcLen);
			if(!pSysProcInf)
			{
				printf("[!] HeapAlloc (%d) :: Error allocating System Process Information.\n", GetLastError());

				FreeLibrary(hNtDLL);
				return;
			}

			ntRet = NtQuerySystemInformation(5, pSysProcInf, dwSysProcLen, &dwSysProcLen);

			while(ntRet == STATUS_INFO_LENGTH_MISMATCH)
			{
				HeapFree(GetProcessHeap(), 0, pSysProcInf);
				pSysProcInf = (SYSTEM_PROCESS_INFORMATION *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSysProcLen);
				ntRet = NtQuerySystemInformation(5, pSysProcInf, dwSysProcLen, &dwSysProcLen);
			}

			pSysProcHead = pSysProcInf;

			if(ntRet == STATUS_SUCCESS)
			{
				while(bMore)
				{
					if(!pSysProcInf->NextEntryOffset)
						bMore = !bMore;

					char szProcUser[64] = { 0 };

					UserFromPID((DWORD)pSysProcInf->UniqueProcessId, szProcUser, NULL);

					printf("- %-8d - %-25s - %-45ws\n", (DWORD)pSysProcInf->UniqueProcessId, szProcUser, (pSysProcInf->ImageName.Length ? pSysProcInf->ImageName.Buffer : L"N/A"));
					
#ifdef _WIN64
					pSysProcInf = (SYSTEM_PROCESS_INFORMATION *)((ULONGLONG)pSysProcInf + (ULONGLONG)pSysProcInf->NextEntryOffset);
#else
					pSysProcInf = (SYSTEM_PROCESS_INFORMATION *)((DWORD)pSysProcInf + (DWORD)pSysProcInf->NextEntryOffset);
#endif
				}
			} 

			HeapFree(GetProcessHeap(), 0, pSysProcHead);

		} else
			printf("[!] GetProcAddress (%d) :: NtQuerySystemInformation API address.\n", GetLastError());

		FreeLibrary(hNtDLL);

	}
}


// Format and display contents of PATH env variable.
void DisplayPATH( void )
{
	char szBuff[32767] = { 0 };
	DWORD dwBuffLen = 32767;

	if(GetEnvironmentVariableA("PATH", szBuff, dwBuffLen))
	{
		printf("\n- PATH:\n\n");

		char *path = strtok(szBuff, ";");
		while(path != NULL)
		{
			printf("- %s\n", path);

			for(size_t i = 0; i < strlen(path) + 2; i++, printf("-")); puts("");

			HANDLE hPathDir = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
			if(hPathDir)
			{
				SECURITY_DESCRIPTOR *pDesc;
				ACL *pDacl = GetObjectDacl(hPathDir, SE_FILE_OBJECT, &pDesc);
				BOOL bIsPresent = FALSE;
				

				GetSecurityDescriptorDacl(&pDesc, &bIsPresent, &pDacl, NULL);

				if(bIsPresent && pDacl == NULL)
					printf("NULL dacl! Everyone - F...");
				else
					PrintDacl(pDacl);


				LocalFree(pDesc);
				CloseHandle(hPathDir);
			}
			
			puts("");
			path = strtok(NULL, ";");
		}
	}
}