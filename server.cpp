#pragma comment(lib, "ws2_32")
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <CommCtrl.h>
#include "resource.h"

#define SERVERIPV4  "127.0.0.1"
#define SERVERPORT  9000
#define BUFSIZE     256                    // 전송 메시지 전체 크기
#define MSGSIZE     (BUFSIZE-sizeof(int)-sizeof(char)*30)  // 채팅 메시지 최대 길이

#define CHATTING			1000
#define IDC_LIST            1002
#define IDC_ENDCONNECT      1004
#define IDC_NOTIFY          1005
#define IDC_NOTIFYBUTTON	1006

#define WM_SOCKET   (WM_USER+1)            
#define REFRESHLIST	(WM_USER+2)
#define WM_ENDCONN	(WM_USER+4)

// 윈도우 메시지 처리 함수
struct SOCKETINFO{
	SOCKET sock;
	char buf[BUFSIZE + 1];
	SOCKADDR_IN clientaddr;
	int recvbytes;
	int sendbytes;
	BOOL recvdelayed;
	char nickname[20];
	SOCKETINFO *next;
};

class CHAT_CLS {
public:
	int  type;
	char address[16];
	char nickname[20];
	char buf[MSGSIZE];
};

int recvn(SOCKET s, char* buf, int len, int flags) {
	int received;
	char* ptr = buf;
	int left = len;

	while (left > 0) {
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR)
			return SOCKET_ERROR;
		else if (received == 0)
			break;
		left -= received;
		ptr += received;
	}
	return (len - left);
}

static HINSTANCE    g_hInst;			//응용 프로그램 인스턴스 핸들
static HWND         g_hWnd;				//윈도우 메시지를 받아주는 핸들러
static HWND         g_hEditStatus;		//받은 메시지 출력
static HWND			hEveryList;			//접속 목록
static HWND			g_hButtonSendMsg;	//공지 전송 버튼
static HWND			g_hEndConn;			//연결 끊기 버튼
static CHAT_CLS		g_chatmsg = CHAT_CLS();
static HANDLE		g_hReadEvent, g_hWriteEvent;//이벤트 핸들

char str[128]; // 내용 받음
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ProcessSocketMessage(HWND, UINT, WPARAM, LPARAM);

// 소켓 관리 함수
BOOL AddSocketInfo(SOCKET sock, char* nickname);
SOCKETINFO *GetSocketInfo(SOCKET sock);
void RemoveSocketInfo(SOCKET sock);

// 오류 출력 함수
void err_quit(const char *msg);
void err_display(const char *msg);
void err_display(int errcode);

int nTotalSockets = 0;
SOCKETINFO *SocketInfoList;
SOCKET listen_sock;
SOCKADDR_IN sockaddr;
int sockaddr_size = sizeof(SOCKADDR_IN);
char msg_head[MSGSIZE];

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);

//스레드
HANDLE hThread;
DWORD WINAPI WriteThread(LPVOID arg);

// 메인 함수
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow){
	int retval;

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	// socket()
	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET) err_quit((char*)"socket()");
	
	//넌블로킹 소켓으로 전환하는 부분!!
	u_long on = 1;
	retval = ioctlsocket(listen_sock, FIONBIO, &on);
	if (retval == SOCKET_ERROR) err_quit("ioctlsocket()");

	//소켓 옵션 설정 부분!!
	BOOL bEnable = TRUE;
	retval = setsockopt(listen_sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&bEnable, sizeof(bEnable));
	if (retval == SOCKET_ERROR) err_quit("setsockopt()");


	// bind()
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);
	retval = bind(listen_sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR) err_quit((char*)"bind()");

	// listen()
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR) err_quit((char*)"listen()");
	
	// 이벤트 생성
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWriteEvent == NULL) return 1;
	hThread = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if (hThread == NULL) {
		MessageBox(NULL, "스레드를 시작할 수 없습니다."
			"\r\n프로그램을 종료합니다.",
			"실패!", MB_ICONERROR);
		exit(1);
	}

	//변수 초기화
	g_chatmsg.type = CHATTING;
	memcpy(g_chatmsg.nickname, "[공 지]", 8);

	// 대화상자 생성
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// 메시지 루프
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	TerminateThread(hThread, 1);
	
	WaitForSingleObject(hThread,INFINITE);
	CloseHandle(hThread);
	// 이벤트 제거
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// 윈속 종료
	WSACleanup();
	return 0;
}

// 데이터 보내기
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;
	int len;
	char tmp[MSGSIZE];

	// 서버와 데이터 통신
	while (1) {
		// 쓰기 완료 기다리기
		WaitForSingleObject(g_hWriteEvent, INFINITE);

		// 문자열 길이가 0이면 보내지 않음
		if (strlen(g_chatmsg.buf) == 0) {
			// '메시지 전송' 버튼 활성화
			EnableWindow(g_hButtonSendMsg, TRUE);
			// 읽기 완료 알리기
			SetEvent(g_hReadEvent);
			continue;
		}
		SOCKETINFO *ptr2 = SocketInfoList;

		// 데이터 보내기
		sprintf_s(tmp, MSGSIZE, "%s %s : %s", msg_head, g_chatmsg.nickname, g_chatmsg.buf);
		memcpy(g_chatmsg.buf, tmp, MSGSIZE);
		for (int i = 0; i < nTotalSockets; i++) {
		SEND_AGAIN:
			
			len = strlen(g_chatmsg.buf) + 40;
			retval = send(ptr2->sock, (char*)&len, sizeof(int), 0);
			if (retval == SOCKET_ERROR) {
				err_display("send()");
				break;
			}
			retval = send(ptr2->sock, (char *)&g_chatmsg, len, 0);
			
			//retval = send(ptr2->sock, (char*)&g_chatmsg, BUFSIZE, 0);
			if (retval == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAEWOULDBLOCK)
					goto SEND_AGAIN;
				break;
			}
			ptr2 = ptr2->next;
		}

		// '메시지 전송' 버튼 활성화
		EnableWindow(g_hButtonSendMsg, TRUE);
		// 읽기 완료 알리기
		SetEvent(g_hReadEvent);
	}

	return 0;
}

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam){
	static HWND hRenewBt;
	static HWND hButtonSendMsg;
	static HWND hEditMsg;

	int retval;
	int i = 0, j = 0;
	
	switch (uMsg) {
	case WM_INITDIALOG:
		// 컨트롤 핸들 얻기
		hEveryList = GetDlgItem(hDlg, IDC_LIST);
		g_hButtonSendMsg = GetDlgItem(hDlg, IDC_NOTIFYBUTTON);
		hEditMsg = GetDlgItem(hDlg, IDC_NOTIFY);
		
		// 컨트롤 초기화
		EnableWindow(hRenewBt, TRUE);
		EnableWindow(g_hButtonSendMsg, TRUE);

		// 윈도우 클래스 등록
		WNDCLASS wndclass;
		wndclass.style = CS_HREDRAW | CS_VREDRAW;
		wndclass.lpfnWndProc = WndProc;
		wndclass.cbClsExtra = 0;
		wndclass.cbWndExtra = 0;
		wndclass.hInstance = g_hInst;
		wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
		wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wndclass.lpszMenuName = NULL;
		wndclass.lpszClassName = "MyWndClass";
		if (!RegisterClass(&wndclass)) return 1;

		// 윈도우 메세지를 처리해줄 윈도우 생성
		g_hWnd = CreateWindow("MyWndClass", "윈도우", WS_CHILD,
			0, 0, 0, 0, hDlg, (HMENU)NULL, g_hInst, NULL);
		if (g_hWnd == NULL) return 1;
		ShowWindow(g_hWnd, SW_SHOW);
		UpdateWindow(g_hWnd);


		retval = WSAAsyncSelect(listen_sock, g_hWnd, WM_SOCKET, FD_ACCEPT | FD_CLOSE);
		return TRUE;

	case WM_COMMAND:

		switch (LOWORD(wParam)) {

		case IDC_LIST:
			switch (HIWORD(wParam)) {
			case LBN_SELCHANGE:
				i = SendMessage(hEveryList, LB_GETCURSEL, 0, 0);
				SendMessage(hEveryList, LB_GETTEXT, i, (LPARAM)str);
				SetWindowText(hEveryList, str);
				EnableWindow(g_hEndConn, TRUE);
			}
			return TRUE;

		case IDC_NOTIFYBUTTON:
			WaitForSingleObject(g_hReadEvent, INFINITE);
			GetDlgItemText(hDlg, IDC_NOTIFY, g_chatmsg.buf, MSGSIZE);
			SetEvent(g_hWriteEvent);
			SetDlgItemText(hDlg, IDC_NOTIFY, "");
			return TRUE;

		case IDC_ENDCONNECT:{
			SOCKETINFO *ptr2 = SocketInfoList;
			char buf[30];
			char buf2[30];
			j = SendMessage(hEveryList, LB_GETCURSEL, 0, 0);
			SendMessage(hEveryList, LB_GETTEXT, j, (LPARAM)buf);
			for (int i = 0; i < nTotalSockets; i++) {
				sprintf(buf2, "%s:%d[%s]", inet_ntoa(ptr2->clientaddr.sin_addr), ntohs(ptr2->clientaddr.sin_port),ptr2->nickname);
				if (strcmp(buf, buf2) == 0) {
					RemoveSocketInfo(ptr2->sock);
				}
				ptr2 = ptr2->next;
			}
			return TRUE;
		}
		case IDCANCEL:
			if (MessageBox(hDlg, "종료하시겠습니까?",
				"알림", MB_YESNO | MB_ICONQUESTION) == IDYES){
				EndDialog(hDlg, IDCANCEL);
			}
			return TRUE;

		}
		return FALSE;
	}

	return FALSE;
}
// 윈도우 메시지 처리
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg){

	case REFRESHLIST:{
		SOCKETINFO *ptr2 = SocketInfoList;
		char buf[100];
		SendMessage(hEveryList, LB_RESETCONTENT, 0, 0);
		for (int i = 0; i < nTotalSockets; i++) {
			sprintf(buf, "%s:%d[%s]", inet_ntoa(ptr2->clientaddr.sin_addr), ntohs(ptr2->clientaddr.sin_port),ptr2->nickname);
			SendMessage(hEveryList, LB_ADDSTRING, 0, (LPARAM)buf);
			ptr2 = ptr2->next;
		}
		return 0;
	}

	case WM_SOCKET: // 소켓 관련 윈도우 메시지
		ProcessSocketMessage(hWnd, uMsg, wParam, lParam);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// 소켓 관련 윈도우 메시지 처리
void ProcessSocketMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// 데이터 통신에 사용할 변수
	SOCKETINFO *ptr;
	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen, retval;
	char nickname[20];

	// 오류 발생 여부 확인
	if (WSAGETSELECTERROR(lParam)) {
		err_display(WSAGETSELECTERROR(lParam));
		RemoveSocketInfo(wParam);
		return;
	}

	// 메시지 처리
	switch (WSAGETSELECTEVENT(lParam)) {
	//accept
	case FD_ACCEPT:
	ACCEPT_AGAIN:
		addrlen = sizeof(clientaddr);

		client_sock = accept(wParam, (SOCKADDR *)&clientaddr, &addrlen);
		retval = recv(client_sock, nickname, 20, 0);
		if (client_sock == INVALID_SOCKET || retval == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				goto ACCEPT_AGAIN;
			err_display((char*)"accept()");
			return;
		}

		retval = WSAAsyncSelect(client_sock, hWnd,
			WM_SOCKET, FD_READ | FD_WRITE | FD_CLOSE);

		AddSocketInfo(client_sock, nickname);
		if (retval == SOCKET_ERROR) {
			err_display((char*)"WSAAsyncSelect()");
			RemoveSocketInfo(client_sock);
		}
		break;

	case FD_READ:{
		printf("read\n");
		ptr = GetSocketInfo(wParam);
		int len;
		// 데이터 받기
	RECEIVE_AGAIN:
		
		retval = recvn(ptr->sock, (char*)&len, sizeof(int), 0);
		if (retval == SOCKET_ERROR) {
			err_display("recv()");
			break;
		}
		else if (retval == 0)
			break;
		ZeroMemory(ptr->buf, MSGSIZE);
		retval = recvn(ptr->sock, ptr->buf, len, 0);
		
		//retval = recvn(ptr->sock, ptr->buf, BUFSIZE, 0);
		if (retval == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				goto RECEIVE_AGAIN;
			err_display((char*)"recv()");
			RemoveSocketInfo(wParam);
			return;
		}

		ptr->recvbytes = retval;

		SOCKETINFO *ptr2 = SocketInfoList;

		for (int i = 0; i < nTotalSockets; i++) {
		SEND_AGAIN:
			
			retval = send(ptr2->sock, (char*)&len, sizeof(int), 0);
			if (retval == SOCKET_ERROR) {
				err_display("send()");
				break;
			}
			retval = send(ptr2->sock, ptr->buf, len, 0);
			
			if (retval == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAEWOULDBLOCK)
					goto SEND_AGAIN;
				err_display((char*)"send()");
				RemoveSocketInfo(ptr2->sock);
			}
			ptr2 = ptr2->next;
		}
	}
	case FD_WRITE:
		break;

	case FD_CLOSE:
		RemoveSocketInfo(wParam);
		break;
	}
}

// 소켓 정보 추가
BOOL AddSocketInfo(SOCKET sock, char* nickname){
	SOCKETINFO *ptr = new SOCKETINFO;
	if (ptr == NULL) {
		printf("[오류] 메모리가 부족합니다!\n");
		return FALSE;
	}

	ptr->sock = sock;
	int addrlen = sizeof(ptr->clientaddr);
	getpeername(sock, (SOCKADDR *)&ptr->clientaddr, &addrlen);
	ptr->recvbytes = 0;
	ptr->sendbytes = 0;
	ptr->recvdelayed = FALSE;
	strcpy(ptr->nickname,nickname);
	ptr->next = SocketInfoList;
	SocketInfoList = ptr;
	nTotalSockets++;
	SendMessage(g_hWnd, REFRESHLIST, NULL, NULL);
	return TRUE;
}

// 소켓 정보 얻기
SOCKETINFO *GetSocketInfo(SOCKET sock){
	SOCKETINFO *ptr = SocketInfoList;

	while (ptr) {
		if (ptr->sock == sock)
			return ptr;
		ptr = ptr->next;
	}

	return NULL;
}

// 소켓 정보 제거
void RemoveSocketInfo(SOCKET sock){
	SOCKADDR_IN clientaddr;
	int addrlen = sizeof(clientaddr);
	getpeername(sock, (SOCKADDR *)&clientaddr, &addrlen);
	printf("[TCP 서버] 클라이언트 종료: IP 주소=%s, 포트 번호=%d\n",
		inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

	SOCKETINFO *curr = SocketInfoList;
	SOCKETINFO *prev = NULL;

	while (curr) {
		if (curr->sock == sock) {
			if (prev)
				prev->next = curr->next;
			else
				SocketInfoList = curr->next;
			closesocket(curr->sock);
			delete curr;
			nTotalSockets--;
			SendMessage(g_hWnd, REFRESHLIST, NULL, NULL);
			return;
		}
		prev = curr;
		curr = curr->next;
	}
}

// 소켓 함수 오류 출력 후 종료
void err_quit(const char *msg){
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}

// 소켓 함수 오류 출력
void err_display(const char *msg){
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char *)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

// 소켓 함수 오류 출력
void err_display(int errcode){
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, errcode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[오류] %s", (char *)lpMsgBuf);
	LocalFree(lpMsgBuf);
}