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

#define BUFSIZE     256										// 전송 메시지 전체 크기
#define MSGSIZE    	(BUFSIZE-sizeof(int))					// 채팅 메시지 최대 길이

#define CHATTING	    1000	//메시지 타입 : 채팅
#define DRAWPEN			1001	//메시지 타입 : 펜으로 그림 그리기
#define DRAWLINE		1002	//메시지 타입 : 직선 그리기
#define DRAWOVAL		1003	//메시지 타입 : 원 그리기
#define DRAWTRI			1004	//메시지 타입 : 삼각형 그리기
#define DRAWRECT		1005	//메시지 타입 : 사각형 그리기
#define DRAWROUNDRECT	1006	//메시지 타입 : 둥근사각형 그리기
#define THICKNESS		1007	//메시지 타입 : 두께 설정
#define ERASE			1008	//메시지 타입 : 지우개
#define CLEAN			1009	//메시지 타입 : 전부 지우기
#define FILLALL			1010	//메시지 타입 : 배경색 칠하기
#define DRAWBELOWTRI	1011	//메시지 타입 : 아래쪽 직각삼각형 그리기
#define DRAWABOVETRI	1012	//메시지 타입 : 위쪽 직각삼각형 그리기
#define DRAWRHONBUS		1013	//메시지 타입 : 마름모 그리기
#define DRAWPENTAGON	1014	//메시지 타입 : 오각형 그리기

#define CLS_ARR_SIZE 100000//도형 저장 크기

//사용자 정의 윈도우 메시지
#define WM_DRAWPEN			(WM_USER+1)           
#define WM_DRAWLINE			(WM_USER+2)
#define WM_DRAWOVAL			(WM_USER+3)
#define WM_DRAWTRI			(WM_USER+4)
#define WM_DRAWRECT			(WM_USER+5)
#define WM_DRAWROUNDRECT	(WM_USER+6)
#define WM_ERASE			(WM_USER+8)
#define WM_CLEAN			(WM_USER+9)
#define WM_FILLALL			(WM_USER+10)
#define WM_DRAWBELOWTRI		(WM_USER+11)
#define WM_DRAWABOVETRI		(WM_USER+12)
#define WM_DRAWRHONBUS		(WM_USER+13)
#define WM_DRAWPENTAGON		(WM_USER+14)

// 채팅 메시지 형식
class COMM_MSG {
public:
	int  type;	
	char address[16];
	char dummy[MSGSIZE];
};

class CHAT_MSG {
public:
	int  type;
	char address[16];
	char nickname[20];
	char buf[MSGSIZE];
};

class DRAWLINE_MSG {
public:
	int  type;
	int  color;
	bool fill_color;
	int  thickness;
	int  x0, y0;
	int  x1, y1;
	char dummy[BUFSIZE - 6 * sizeof(int)];
};


static HINSTANCE     g_hInst;					 //응용 프로그램 인스턴스 핸들
static HWND          g_hDrawWnd;				 //그림을 그릴 윈도우
static HWND          g_hButtonSendMsg;			 //전송 버튼
static HWND          g_hEditStatus;				 //받은 메시지 출력
static char          g_ipaddr[64];				 //서버 IP 주소
static u_short       g_port;					 //서버 포트 번호
static HANDLE        g_hClientThread;			 //스레드 핸들
static HANDLE        g_hReadEvent, g_hWriteEvent;//이벤트 핸들
static volatile BOOL g_bStart;					 //통신 시작 여부
static SOCKET        g_sock;					 //클라이언트 소켓
static CHAT_MSG      g_chatmsg = CHAT_MSG();     //채팅 메시지 저장
static DRAWLINE_MSG  g_drawmsg = DRAWLINE_MSG(); //선 그리기 메시지 저장 
static HWND			 g_hButtonConnect;

static int           g_drawtool;				 //그리기 도구
static int           g_drawcolor;				 //선 그리기 색상 
static int           g_drawthickness;			 //선 굵기
static bool          g_drawfill;				 //채우기

char tmp2[MSGSIZE];
static DRAWLINE_MSG *class_array[CLS_ARR_SIZE];   
static int pointer = 0;
SOCKADDR_IN sockaddr;
int sockaddr_size = sizeof(SOCKADDR_IN);
char msg_head[MSGSIZE];

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// 자식 윈도우 프로시저
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
// 편집 컨트롤 출력 함수
void DisplayText(char *fmt, ...);
// 사용자 정의 데이터 수신 함수
int recvn(SOCKET s, char *buf, int len, int flags);
// 오류 출력 함수
void err_quit(const char *msg);
void err_display(const char *msg);

// 메인 함수
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	// 이벤트 생성
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWriteEvent == NULL) return 1;

	// 변수 초기화(일부)
	g_chatmsg.type = CHATTING;       
	g_drawmsg.type = DRAWPEN;
	g_drawtool = DRAWPEN;
	g_drawcolor = RGB(0, 0, 0);
	g_drawfill = false;      
	g_drawmsg.color = RGB(0, 0, 0);
	g_drawmsg.fill_color = false;

	// 대화상자 생성
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// 이벤트 제거
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// 윈속 종료
	WSACleanup();
	return 0;
}

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HWND hButtonIsIPv6;
	static HWND hEditIPaddr;
	static HWND hEditPort;
	static HWND hButtonUnConnect;
	static HWND hEditMsg;
	static HWND hPen;
	static HWND hErase;
	static HWND hLine;
	static HWND hOval;
	static HWND hTriangle;
	static HWND hBelowTri;
	static HWND hAboveTri;
	static HWND hRhonBus;
	static HWND hPentagon;
	static HWND hRoundrect;
	static HWND hColorRed;
	static HWND hColorOrange;
	static HWND hColorYellow;
	static HWND hColorGreen;
	static HWND hColorBlue;
	static HWND hColorIndigo;
	static HWND hColorPurple;
	static HWND hColorBlack;
	static HWND hThickness;
	static HWND hFillall;
	int pos;

	switch (uMsg) {

		//초기화
	case WM_INITDIALOG:
		// 컨트롤 핸들 얻기
		hEditIPaddr = GetDlgItem(hDlg, IDC_IPADDR);
		hEditPort = GetDlgItem(hDlg, IDC_PORT);
		g_hButtonConnect = GetDlgItem(hDlg, IDC_CONNECT);
		hButtonUnConnect = GetDlgItem(hDlg, IDC_UNCONNECT);
		g_hButtonSendMsg = GetDlgItem(hDlg, IDC_SENDMSG);
		hEditMsg = GetDlgItem(hDlg, IDC_MSG);
		g_hEditStatus = GetDlgItem(hDlg, IDC_STATUS);
		hPen = GetDlgItem(hDlg, IDC_PEN);
		hErase = GetDlgItem(hDlg, IDC_ERASE);
		hLine = GetDlgItem(hDlg, IDC_LINE);
		hOval = GetDlgItem(hDlg, IDC_OVAL);
		hTriangle = GetDlgItem(hDlg, IDC_TRIANGLE);
		hRoundrect = GetDlgItem(hDlg, IDC_ROUNDRECT);
		hColorRed = GetDlgItem(hDlg, IDC_COLORRED);
		hColorGreen = GetDlgItem(hDlg, IDC_COLORGREEN);
		hColorBlue = GetDlgItem(hDlg, IDC_COLORBLUE);
		hColorYellow = GetDlgItem(hDlg, IDC_COLORYELLOW);
		hColorOrange = GetDlgItem(hDlg, IDC_COLORORANGE);
		hColorIndigo = GetDlgItem(hDlg, IDC_COLORIndigo);
		hColorPurple = GetDlgItem(hDlg, IDC_COLORPURPLE);
		hColorBlack = GetDlgItem(hDlg, IDC_COLORBLACK);
		hThickness = GetDlgItem(hDlg, IDC_SLIDER);
		hFillall = GetDlgItem(hDlg, IDC_FILLALL);
		hBelowTri = GetDlgItem(hDlg, IDC_BELOWTRIANGLE);
		hAboveTri = GetDlgItem(hDlg, IDC_ABOVETRIANGLE);
		hRhonBus = GetDlgItem(hDlg, IDC_RHONBUS);
		hPentagon = GetDlgItem(hDlg, IDC_PENTAGON);

		EnableWindow(hButtonUnConnect, FALSE);
		SetDlgItemText(hDlg, IDC_IPADDR2, g_chatmsg.nickname);

		SendMessage(hEditMsg, EM_SETLIMITTEXT, MSGSIZE, 0);
		EnableWindow(g_hButtonSendMsg, FALSE);
		EnableWindow(hErase, TRUE);
		SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
		SetDlgItemInt(hDlg, IDC_PORT, SERVERPORT, FALSE);
		SendMessage(hColorRed, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorGreen, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorBlue, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorBlack, BM_SETCHECK, BST_CHECKED, 0);
		SendMessage(hColorOrange, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorIndigo, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorYellow, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hColorPurple, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hPen, BM_SETCHECK, BST_CHECKED, 0);
		SendMessage(hErase, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hLine, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hOval, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hTriangle, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hRoundrect, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hThickness, TBM_SETRANGE, FALSE, MAKELPARAM(1, 10)); // 선의 굵기는 1에서부터 10까지 설정이 가능하기 때문에 파라미터로 1,10을 전달
		SendMessage(hThickness, TBM_SETPOS, TRUE, 1); // default값은 1로 설정
		SendMessage(hBelowTri, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hAboveTri, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hRhonBus, BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(hPentagon, BM_SETCHECK, BST_UNCHECKED, 0);
		
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

		//그림판윈도우 생성
		g_hDrawWnd = CreateWindow("MyWndClass", "그림 그릴 윈도우", WS_CHILD,
			450, 20, 395, 365, hDlg, (HMENU)NULL, g_hInst, NULL);
		if (g_hDrawWnd == NULL) return 1;
		ShowWindow(g_hDrawWnd, SW_SHOW);
		UpdateWindow(g_hDrawWnd);
		return TRUE;

		//굵기
	case WM_HSCROLL:
		pos = SendDlgItemMessage(hDlg, IDC_SLIDER, TBM_GETPOS, 0, 0);
		g_drawmsg.thickness = pos;
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_CONNECT:

			GetDlgItemText(hDlg, IDC_IPADDR, g_ipaddr, sizeof(g_ipaddr));
			g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);
			GetDlgItemText(hDlg, IDC_IPADDR2, g_chatmsg.nickname, sizeof(g_chatmsg.nickname));

			// 소켓 통신 스레드 시작
			g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
			if (g_hClientThread == NULL) {
				MessageBox(hDlg, "클라이언트를 시작할 수 없습니다."
					"\r\n프로그램을 종료합니다.", "실패!", MB_ICONERROR);
				EndDialog(hDlg, 0);
			}
			else {
				EnableWindow(g_hButtonConnect, FALSE);
				EnableWindow(hButtonUnConnect, TRUE);
				while (g_bStart == FALSE); // 서버 접속 성공 기다림
				EnableWindow(hEditIPaddr, FALSE);
				EnableWindow(hEditPort, FALSE);
				EnableWindow(g_hButtonSendMsg, TRUE);
				SetFocus(hEditMsg);
			}
			return TRUE;
		case IDC_SENDMSG:
			// 읽기 완료를 기다림
			WaitForSingleObject(g_hReadEvent, INFINITE);
			GetDlgItemText(hDlg, IDC_MSG, g_chatmsg.buf, MSGSIZE);
			// 쓰기 완료를 알림
			SetEvent(g_hWriteEvent);
			// 텍스트 비움
			SetDlgItemText(hDlg, IDC_MSG, "");
			return TRUE;

		case IDC_COLORRED:
			g_drawmsg.color = RGB(255, 0, 0);
			return TRUE;
		case IDC_COLORORANGE:
			g_drawmsg.color = RGB(255, 128, 0);
			return TRUE;
		case IDC_COLORYELLOW:
			g_drawmsg.color = RGB(255, 255, 0);
			return TRUE;
		case IDC_COLORGREEN:
			g_drawmsg.color = RGB(0, 255, 0);
			return TRUE;
		case IDC_COLORBLUE:
			g_drawmsg.color = RGB(0, 0, 255);
			return TRUE;
		case IDC_COLORIndigo:
			g_drawmsg.color = RGB(0, 0, 128);
			return TRUE;
		case IDC_COLORPURPLE:
			g_drawmsg.color = RGB(128, 0, 128);
			return TRUE;
		case IDC_COLORBLACK:
			g_drawmsg.color = RGB(0, 0, 0);
			return TRUE;
		case IDC_FILL:
			g_drawmsg.fill_color = g_drawmsg.fill_color == true ? false : true;
			return TRUE;
		case IDC_PEN:
			g_drawmsg.type = DRAWPEN;
			return TRUE;
		case IDC_ERASE:
			g_drawmsg.type = ERASE;
			return TRUE;
		case IDC_LINE:
			g_drawmsg.type = DRAWLINE;
			return TRUE;
		case IDC_OVAL:
			g_drawmsg.type = DRAWOVAL;
			return TRUE;		
		case IDC_TRIANGLE:
			g_drawmsg.type = DRAWTRI;
			return TRUE;	
		case IDC_RECTANGLE:
			g_drawmsg.type = DRAWRECT;
			return TRUE;		
		case IDC_ROUNDRECT:
			g_drawmsg.type = DRAWROUNDRECT;
			return TRUE;
		case IDC_ERASEALL:
			g_drawmsg.type = CLEAN;
			return TRUE;
		case IDC_FILLALL:
			g_drawmsg.type = FILLALL;
			return TRUE;
		case IDC_BELOWTRIANGLE:
			g_drawmsg.type = DRAWBELOWTRI;
			return TRUE;
		case IDC_ABOVETRIANGLE:
			g_drawmsg.type = DRAWABOVETRI;
			return TRUE;
		case IDC_RHONBUS:
			g_drawmsg.type = DRAWRHONBUS;
			return TRUE;
		case IDC_PENTAGON:
			g_drawmsg.type = DRAWPENTAGON;
			return TRUE;

		case IDCANCEL:
			if (MessageBox(hDlg, "종료하시겠습니까?",
				"알림", MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				closesocket(g_sock);
				EndDialog(hDlg, IDCANCEL);
			}
			return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg)
{
	int retval;

	// socket()
	g_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (g_sock == INVALID_SOCKET) err_quit("socket()");

	// connect()
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(g_ipaddr);
	serveraddr.sin_port = htons(g_port);
	retval = connect(g_sock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR 
		|| send(g_sock, (char *)&g_chatmsg.nickname, 20, 0) == SOCKET_ERROR) 
		err_quit("connect()");
	getsockname(g_sock, (SOCKADDR*)&sockaddr, &sockaddr_size);
	sprintf(msg_head, "[%s:%u]", inet_ntoa(sockaddr.sin_addr), ntohs(sockaddr.sin_port));

	MessageBox(NULL, "서버에 접속했습니다.", "성공!", MB_ICONINFORMATION);

	// 읽기 & 쓰기 스레드 생성
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if (hThread[0] == NULL || hThread[1] == NULL) {
		MessageBox(NULL, "스레드를 시작할 수 없습니다."
			"\r\n프로그램을 종료합니다.",
			"실패!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = TRUE;

	// 스레드 종료 대기
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval -= WAIT_OBJECT_0;
	if (retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = FALSE;

	MessageBox(NULL, "서버가 접속을 끊었습니다", "알림", MB_ICONINFORMATION);-
	EnableWindow(g_hButtonSendMsg, FALSE);
	EnableWindow(g_hButtonConnect, TRUE);
	closesocket(g_sock);
	return 0;
}

// 데이터 받기
DWORD WINAPI ReadThread(LPVOID arg)
{
	int retval;
	int len;
	COMM_MSG comm_msg;
	CHAT_MSG *chat_msg;
	DRAWLINE_MSG *draw_msg;

	while (1) {
		
		retval = recvn(g_sock, (char*)&len, sizeof(int), 0);
		if (retval == SOCKET_ERROR) {
			err_display("recv()");
			break;
		}
		else if (retval == 0)
			break;
		ZeroMemory(comm_msg.dummy, MSGSIZE);
		retval = recvn(g_sock, (char *)&comm_msg, len, 0);
		
		//retval = recvn(g_sock, (char *)&comm_msg, BUFSIZE, 0);
		if (retval == 0 || retval == SOCKET_ERROR) {
			break;
		}
		if (comm_msg.type == CHATTING) {
			chat_msg = (CHAT_MSG *)&comm_msg;
			DisplayText((char *)"%s\r\n", chat_msg->buf);
		}
		else {
			draw_msg = (DRAWLINE_MSG *)&comm_msg;
			class_array[pointer++] = draw_msg;
			g_drawtool = draw_msg->type;
			g_drawcolor = draw_msg->color;
			g_drawthickness = draw_msg->thickness;
			g_drawfill = draw_msg->fill_color;
			SendMessage(g_hDrawWnd, WM_USER + (g_drawtool - 1000),
				MAKEWPARAM(draw_msg->x0, draw_msg->y0),
				MAKELPARAM(draw_msg->x1, draw_msg->y1));
		}
	}
	return 0;
}


// 데이터 보내기
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;
	int len;
	char buf[BUFSIZE];
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
		// 데이터 보내기
		sprintf_s(tmp, MSGSIZE, "%s %s : %s", msg_head, g_chatmsg.nickname, g_chatmsg.buf);
		memcpy(g_chatmsg.buf, tmp, MSGSIZE);
		
		len = strlen(g_chatmsg.buf) + 40;
		//strncpy(buf, g_chatmsg.buf, len);
		retval = send(g_sock, (char*)&len, sizeof(int), 0);
		if (retval == SOCKET_ERROR) {
			err_display("send()");
			break;
		}
		retval = send(g_sock, (char *)&g_chatmsg, len, 0);
		
		//retval = send(g_sock, (char *)&g_chatmsg, BUFSIZE, 0);
		if (retval == SOCKET_ERROR) {
			break;
		}

		// '메시지 전송' 버튼 활성화
		EnableWindow(g_hButtonSendMsg, TRUE);
		// 읽기 완료 알리기
		SetEvent(g_hReadEvent);
	}

	return 0;
}

// 자식 윈도우 프로시저
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	int cx, cy;
	PAINTSTRUCT ps;
	RECT rect;
	HPEN hPen, hOldPen;
	static HBITMAP hBitmap;
	static HDC hDCMem;
	static int x0, y0;
	static int x1, y1;
	static BOOL bDrawing = FALSE;
	
	switch (uMsg) {
	case WM_CREATE:
		hDC = GetDC(hWnd);

		// 화면을 저장할 비트맵 생성
		cx = GetDeviceCaps(hDC, HORZRES);
		cy = GetDeviceCaps(hDC, VERTRES);
		hBitmap = CreateCompatibleBitmap(hDC, cx, cy);

		// 메모리 DC 생성
		hDCMem = CreateCompatibleDC(hDC);

		// 비트맵 선택 후 메모리 DC 화면을 흰색으로 칠함
		SelectObject(hDCMem, hBitmap);
		SelectObject(hDCMem, GetStockObject(WHITE_BRUSH));
		SelectObject(hDCMem, GetStockObject(WHITE_PEN));
		Rectangle(hDCMem, 0, 0, cx, cy);

		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_LBUTTONDOWN:
		x0 = LOWORD(lParam);
		y0 = HIWORD(lParam);
		bDrawing = TRUE;
		return 0;

	case WM_MOUSEMOVE:
		int len;
		if (bDrawing && g_bStart && (g_drawmsg.type == DRAWPEN || g_drawmsg.type == ERASE)) {
			x1 = LOWORD(lParam);
			y1 = HIWORD(lParam);

			// 선 그리기 메시지 보내기
			g_drawmsg.x0 = x0;
			g_drawmsg.y0 = y0;
			g_drawmsg.x1 = x1;
			g_drawmsg.y1 = y1;

			len = sizeof(DRAWLINE_MSG);
			send(g_sock, (char *)&len, sizeof(int), 0);
			send(g_sock, (char *)&g_drawmsg, len, 0);


			x0 = x1;
			y0 = y1;
		}
		return 0;

	case WM_LBUTTONUP:
		if (bDrawing && g_bStart && (g_drawmsg.type != DRAWPEN || g_drawmsg.type != ERASE)) {
			x1 = LOWORD(lParam);
			y1 = HIWORD(lParam);

			// 선 그리기 메시지 보내기
			g_drawmsg.x0 = x0;
			g_drawmsg.y0 = y0;
			g_drawmsg.x1 = x1;
			g_drawmsg.y1 = y1;

			len = sizeof(DRAWLINE_MSG);
			send(g_sock, (char *)&len, sizeof(int), 0);
			send(g_sock, (char *)&g_drawmsg, len, 0);

			x0 = x1;
			y0 = y1;
		}
		bDrawing = FALSE;
		return 0;

	case WM_DRAWPEN:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);

		// 화면에 그리기
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
		LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
		SelectObject(hDC, hOldPen);

		DeleteObject(hPen);
		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_ERASE:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, RGB(255, 255, 255));

		// 화면에 그리기
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
		LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
		SelectObject(hDC, hOldPen);

		DeleteObject(hPen);
		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_DRAWLINE:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);

		// 화면에 그리기
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
		LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
		SelectObject(hDC, hOldPen);

		DeleteObject(hPen);
		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_DRAWOVAL:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		if (LOWORD(wParam) > LOWORD(lParam)) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
		}
		else {
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
		}
		if (HIWORD(wParam) > HIWORD(lParam)) {
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);
		}
		else {
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);
		}

		if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
		else SelectObject(hDC, GetStockObject(NULL_BRUSH));
		Ellipse(hDC, x0, y0, x1, y1);
		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_DRAWTRI:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		if (LOWORD(wParam) > LOWORD(lParam)) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
		}
		else {
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
		}
		if (HIWORD(wParam) > HIWORD(lParam)) {
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);
		}
		else {
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);
		}
		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
		else SelectObject(hDC, GetStockObject(NULL_BRUSH));
		
		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x0, y1, NULL);
		LineTo(hDC, x1, y1);
		SelectObject(hDC, hOldPen);

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x1, y1, NULL);
		LineTo(hDC, x0 + (x1 - x0) / 2, y0);
		SelectObject(hDC, hOldPen);

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x0 + (x1 - x0) / 2, y0, NULL);
		LineTo(hDC, x0, y1);
		SelectObject(hDC, hOldPen);

		ReleaseDC(hWnd, hDC);

		return 0;

	case WM_DRAWBELOWTRI:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		if ((LOWORD(wParam) > LOWORD(lParam)) && ((HIWORD(wParam) > HIWORD(lParam)))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);
		}
		else if((LOWORD(wParam) > LOWORD(lParam)) && ((HIWORD(wParam) < HIWORD(lParam)))){
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);
		}
		else if ((LOWORD(wParam) < LOWORD(lParam)) && ((HIWORD(wParam) > HIWORD(lParam)))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);
		}
		else if ((LOWORD(wParam) < LOWORD(lParam)) && ((HIWORD(wParam) < HIWORD(lParam)))) {
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);
		}
		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
		else SelectObject(hDC, GetStockObject(NULL_BRUSH));

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x0, y0, NULL);
		LineTo(hDC, x1, y1);
		SelectObject(hDC, hOldPen);

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x1, y1, NULL);
		LineTo(hDC, x0, y1);
		SelectObject(hDC, hOldPen);

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x0, y1, NULL);
		LineTo(hDC, x0, y0);
		SelectObject(hDC, hOldPen);

		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_DRAWABOVETRI:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		if ((LOWORD(wParam) > LOWORD(lParam)) && ((HIWORD(wParam) > HIWORD(lParam)))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);
		}
		else if ((LOWORD(wParam) > LOWORD(lParam)) && ((HIWORD(wParam) < HIWORD(lParam)))) {
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);
		}
		else if ((LOWORD(wParam) < LOWORD(lParam)) && ((HIWORD(wParam) > HIWORD(lParam)))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);
		}
		else if ((LOWORD(wParam) < LOWORD(lParam)) && ((HIWORD(wParam) < HIWORD(lParam)))) {
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);
		}
		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
		else SelectObject(hDC, GetStockObject(NULL_BRUSH));

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x0, y0, NULL);
		LineTo(hDC, x1, y1);
		SelectObject(hDC, hOldPen);

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x1, y1, NULL);
		LineTo(hDC, x1, y0);
		SelectObject(hDC, hOldPen);

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x1, y0, NULL);
		LineTo(hDC, x0, y0);
		SelectObject(hDC, hOldPen);

		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_DRAWRHONBUS:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		if ((LOWORD(wParam) > LOWORD(lParam)) && (HIWORD(wParam) > HIWORD(lParam))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
			else SelectObject(hDC, GetStockObject(NULL_BRUSH));

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y0, NULL);
			LineTo(hDC, x1, y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x1, y1, NULL);
			LineTo(hDC, x0, y1 + (y1 - y0));
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y1 + (y1 - y0), NULL);
			LineTo(hDC, x0 - (x1 - x0), y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 - (x1 - x0), y1, NULL);
			LineTo(hDC, x0, y0);
			SelectObject(hDC, hOldPen);

			ReleaseDC(hWnd, hDC);

			return 0;
		}
		else if((LOWORD(wParam) > LOWORD(lParam)) && (HIWORD(wParam) < HIWORD(lParam))){
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
			else SelectObject(hDC, GetStockObject(NULL_BRUSH));

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y0, NULL);
			LineTo(hDC, x0 + (x0 - x1), y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 + (x0 - x1), y1, NULL);
			LineTo(hDC, x0, y1 + (y1 - y0));
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y1 + (y1 - y0), NULL);
			LineTo(hDC, x1, y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x1, y1, NULL);
			LineTo(hDC, x0, y0);
			SelectObject(hDC, hOldPen);

			ReleaseDC(hWnd, hDC);

			return 0;
		}
		else if ((LOWORD(wParam) < LOWORD(lParam)) && (HIWORD(wParam) > HIWORD(lParam))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
			else SelectObject(hDC, GetStockObject(NULL_BRUSH));

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y0, NULL);
			LineTo(hDC, x0 + (x0 - x1), y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 + (x0 - x1), y1, NULL);
			LineTo(hDC, x0, y1 + (y1 - y0));
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y1 + (y1 - y0), NULL);
			LineTo(hDC, x1, y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x1, y1, NULL);
			LineTo(hDC, x0, y0);
			SelectObject(hDC, hOldPen);

			ReleaseDC(hWnd, hDC);

			return 0;
		}

		else if ((LOWORD(wParam) < LOWORD(lParam)) && (HIWORD(wParam) < HIWORD(lParam))) {
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
			else SelectObject(hDC, GetStockObject(NULL_BRUSH));

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y0, NULL);
			LineTo(hDC, x1, y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x1, y1, NULL);
			LineTo(hDC, x0, y1 + (y1 - y0));
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y1 + (y1 - y0), NULL);
			LineTo(hDC, x0 - (x1 - x0), y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 - (x1 - x0), y1, NULL);
			LineTo(hDC, x0, y0);
			SelectObject(hDC, hOldPen);

			ReleaseDC(hWnd, hDC);

			return 0;
		}

	case WM_DRAWRECT:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		if ((LOWORD(wParam) > LOWORD(lParam)) && (HIWORD(wParam) > HIWORD(lParam))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);
		}
		else if ((LOWORD(wParam) > LOWORD(lParam)) && (HIWORD(wParam) < HIWORD(lParam))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);
		}
		else if((LOWORD(wParam) < LOWORD(lParam)) && (HIWORD(wParam) > HIWORD(lParam))){
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);
		}
		else if((LOWORD(wParam) < LOWORD(lParam)) && (HIWORD(wParam) < HIWORD(lParam))){
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);
		}
		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
		else SelectObject(hDC, GetStockObject(NULL_BRUSH));
		/*Rectangle(hDC, x0, y0, x1, y1);
		ReleaseDC(hWnd, hDC);*/

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x1, y0, NULL);
		LineTo(hDC, x1, y1);
		SelectObject(hDC, hOldPen);

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x1, y1, NULL);
		LineTo(hDC, x0, y1);
		SelectObject(hDC, hOldPen);
	
		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x0, y1, NULL);
		LineTo(hDC, x0, y0);
		SelectObject(hDC, hOldPen);

		hOldPen = (HPEN)SelectObject(hDCMem, hPen);
		MoveToEx(hDC, x0, y0, NULL);
		LineTo(hDC, x1, y0);
		SelectObject(hDC, hOldPen);

		ReleaseDC(hWnd, hDC);

		return 0;

	case WM_DRAWPENTAGON:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		if ((LOWORD(wParam) > LOWORD(lParam)) && ((HIWORD(wParam) > HIWORD(lParam)))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
			else SelectObject(hDC, GetStockObject(NULL_BRUSH));

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y0, NULL);
			LineTo(hDC, x1, y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x1, y1, NULL);
			LineTo(hDC, x0 + (x1 - x0) / 2 , y1 + (y1 - y0) * 2);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 + (x1 - x0) / 2, y1 + (y1 - y0) * 2, NULL);
			LineTo(hDC, x0 - (x1 - x0) / 2, y1 + (y1 - y0) * 2);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 - + (x1 - x0) / 2, y1 + (y1 - y0) * 2, NULL);
			LineTo(hDC, x0 - + (x1 - x0), y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 - +(x1 - x0), y1, NULL);
			LineTo(hDC, x0, y0);
			SelectObject(hDC, hOldPen);

			ReleaseDC(hWnd, hDC);
			return 0;
		}
		else if ((LOWORD(wParam) > LOWORD(lParam)) && ((HIWORD(wParam) < HIWORD(lParam)))) {
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
			else SelectObject(hDC, GetStockObject(NULL_BRUSH));

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y0, NULL);
			LineTo(hDC, x0 + (x0 - x1), y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 + (x0 - x1), y1, NULL);
			LineTo(hDC, x0 + (x0 - x1) / 2, y1 + (y1 - y0) * 2);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 + (x0 - x1) / 2, y1 + (y1 - y0) * 2, NULL);
			LineTo(hDC, x0 - (x0 - x1) / 2, y1 + (y1 - y0) * 2);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 - (x0 - x1) / 2, y1 + (y1 - y0) * 2, NULL);
			LineTo(hDC, x1, y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x1, y1, NULL);
			LineTo(hDC, x0, y0);
			SelectObject(hDC, hOldPen);

			ReleaseDC(hWnd, hDC);

			return 0;

		}
		else if ((LOWORD(wParam) < LOWORD(lParam)) && ((HIWORD(wParam) > HIWORD(lParam)))) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
			else SelectObject(hDC, GetStockObject(NULL_BRUSH));

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y0, NULL);
			LineTo(hDC, x1, y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x1, y1, NULL);
			LineTo(hDC, x0 + (x1 - x0) / 2, y1 + (y1 - y0) * 2);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 + (x1 - x0) / 2, y1 + (y1 - y0) * 2, NULL);
			LineTo(hDC, x0 - (x1 - x0) / 2, y1 + (y1 - y0) * 2);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 - +(x1 - x0) / 2, y1 + (y1 - y0) * 2, NULL);
			LineTo(hDC, x0 - +(x1 - x0), y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 - +(x1 - x0), y1, NULL);
			LineTo(hDC, x0, y0);
			SelectObject(hDC, hOldPen);

			ReleaseDC(hWnd, hDC);

			return 0;
		}
		else if ((LOWORD(wParam) < LOWORD(lParam)) && ((HIWORD(wParam) < HIWORD(lParam)))) {
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
			else SelectObject(hDC, GetStockObject(NULL_BRUSH));

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0, y0, NULL);
			LineTo(hDC, x0 + (x0 - x1), y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 + (x0 - x1), y1, NULL);
			LineTo(hDC, x0 + (x0 - x1) / 2, y1 + (y1 - y0) * 2);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 + (x0 - x1) / 2, y1 + (y1 - y0) * 2, NULL);
			LineTo(hDC, x0 - (x0 - x1) / 2, y1 + (y1 - y0) * 2);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x0 - (x0 - x1) / 2, y1 + (y1 - y0) * 2, NULL);
			LineTo(hDC, x1, y1);
			SelectObject(hDC, hOldPen);

			hOldPen = (HPEN)SelectObject(hDCMem, hPen);
			MoveToEx(hDC, x1, y1, NULL);
			LineTo(hDC, x0, y0);
			SelectObject(hDC, hOldPen);

			ReleaseDC(hWnd, hDC);

			return 0;
		}

	case WM_DRAWROUNDRECT:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		if (LOWORD(wParam) > LOWORD(lParam)) {
			x0 = LOWORD(lParam);
			x1 = LOWORD(wParam);
		}
		else {
			x0 = LOWORD(wParam);
			x1 = LOWORD(lParam);
		}
		if (HIWORD(wParam) > HIWORD(lParam)) {
			y0 = HIWORD(lParam);
			y1 = HIWORD(wParam);
		}
		else {
			y0 = HIWORD(wParam);
			y1 = HIWORD(lParam);
		}

		if (g_drawfill == true)SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
		else SelectObject(hDC, GetStockObject(NULL_BRUSH));
		RoundRect(hDC, x0, y0, x1, y1, 10, 10);
		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_CLEAN:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, RGB(255, 255, 255));
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		SelectObject(hDC, GetStockObject(WHITE_BRUSH));
		Rectangle(hDC, 0, 0, 1000, 1000);
		ReleaseDC(hWnd, hDC);
		return 0;
	
	case WM_FILLALL:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawthickness, g_drawcolor);
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		SelectObject(hDC, (HBRUSH)CreateSolidBrush(g_drawcolor));
		Rectangle(hDC, 0, 0, 1000, 1000);
		ReleaseDC(hWnd, hDC);
		return 0;

	case WM_DESTROY:
		DeleteObject(hBitmap);
		DeleteDC(hDCMem);
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// 에디트 컨트롤에 문자열 출력
void DisplayText(char *fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);

	int nLength = GetWindowTextLength(g_hEditStatus);
	SendMessage(g_hEditStatus, EM_SETSEL, nLength, nLength);
	SendMessage(g_hEditStatus, EM_REPLACESEL, FALSE, (LPARAM)cbuf);

	va_end(arg);
}

// 사용자 정의 데이터 수신 함수
int recvn(SOCKET s, char *buf, int len, int flags)
{
	int received;
	char *ptr = buf;
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

// 소켓 함수 오류 출력 후 종료
void err_quit(const char *msg)
{
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
void err_display(const char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char *)lpMsgBuf);
	LocalFree(lpMsgBuf);
}