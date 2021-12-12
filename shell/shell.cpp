#include<Windows.h>
#include<iostream>
#include<tchar.h>
#include<fstream>
#include<string>
#include<unordered_set>

using namespace std;

HANDLE mapShare;
HANDLE user, server, write_mem, fileW;
char* pBegin;
char IPC_buf[1024];

bool writing = false;

void login(string& name)
{
	fstream f;
	unordered_set<string> users;
	f.open("users.txt", ios::in);
	if (!f.is_open())
		cout << "open failed" << endl;
	string line;
	while (!f.eof())
	{
		getline(f, line);
		users.insert(line);
	}
	f.close();
	cout << "login" << endl;
	while (true)
	{
		string s;
		cin >> s;
		if (users.find(s) != users.end())
		{
			cout << "login successfully" << endl;
			name = s;
			break;
		}
		else
			cout << "invalid user name" << endl;
	}
}

void fmt(char* s)
{
	int st = strlen(s);
	for (int i = st; i < 20; ++i)
		s[i] = ' ';
	s[19] = 0;
}

void share_mem()
{
	if ((mapShare = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, _T("file_system"))) == INVALID_HANDLE_VALUE)
	{
		printf("OpenFileMapping\n");
		return;
	}
	if ((pBegin = (char*)MapViewOfFile(mapShare, FILE_MAP_ALL_ACCESS, 0, 0, 1024)) == NULL)
	{
		printf("MapViewOfFile\n");
		return;
	}
	string name;
	login(name);
	string fileWrite("fileW");
	fileW = OpenEvent(EVENT_ALL_ACCESS, FALSE, LPCWSTR(fileWrite.c_str()));
	write_mem = OpenEvent(EVENT_ALL_ACCESS, FALSE, LPCWSTR("write"));
	user = OpenEvent(EVENT_ALL_ACCESS, FALSE, LPCWSTR(name.c_str()));
	if (user == 0)
		cout << "not ok" << endl;
	server = OpenEvent(EVENT_ALL_ACCESS, FALSE, _T("serverEvent"));
	getchar();
	while (true)
	{
		WaitForSingleObject(user, INFINITE);
		strcpy_s(IPC_buf, 1024, pBegin);
		printf("from fs:%s\n", IPC_buf);
		printf("$ ");
		fgets(IPC_buf, 1024, stdin);
		IPC_buf[strlen(IPC_buf) - 1] = '\0';
		char fmtname[20];
		strcpy_s(fmtname, 20, name.c_str());
		fmt(fmtname);
		if (!writing)
			WaitForSingleObject(fileW, INFINITE);
		ResetEvent(write_mem);
		strcpy_s(pBegin, 20, fmtname);
		strcpy_s(pBegin+20, 1024, IPC_buf);
		SetEvent(write_mem);
		ResetEvent(user);
		SetEvent(server);
		if (strncmp(IPC_buf, "write", 5) == 0)
		{
			writing = true;
		}
		else
		{
			writing = false;
		}
		if (strncmp(IPC_buf, "exit", 4) == 0)
			break;
	}
	CloseHandle(user);
	CloseHandle(server);
	CloseHandle(write_mem);
	UnmapViewOfFile(pBegin);
	CloseHandle(mapShare);
}

int main()
{
	share_mem();
}