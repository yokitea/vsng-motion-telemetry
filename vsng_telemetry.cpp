#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <math.h>
#include <stdio.h>

// Struct definition from VSNG SDK
struct panel_info {
	bool*		pilot_on;
	bool*		light_on;
	bool*		engine_on;
	bool*		stall;

	bool*		brakes_on;
	bool*		gear_down;
	bool*		cable_down;

	int*		flaps_stage;
	int*		abrakes_stage;

	float*		motor_rpm;
	float*		motor_rpm1;
	float*		motor_rpm2;
	float*		fuel_left;

	float*		alfa;
	float*		beta;
	float*		gama;
	float*		delta;
	float*		phi;

	float*		velocity;
	float*		alt;
	float*		vert;
	float*		dbet;
	float*		accel;
	float*		height;

	int*		next_waypoint;
	float*		waypoint_beta;
	float*		waypoint_dist;

	float*		east;
	float*		north;

	char*		scenery;
	char*		vehicle;

	float*		flaps_angle;
	float*		brakes_angle;
	float*		abrakes_angle;
	float*		trim_angle;
	float*		gear_angle;

	float*		omega;
	float*		torque;

	float*		control_phi;
	float*		control_alfa;
	float*		control_delta;

	float*		control_rpm;
	float*		control_rpm1;
	float*		control_rpm2;

	float*		control_pitch;
	float*		control_mixture;
};

// Global variables
panel_info* m_info = NULL;
SOCKET m_socket = INVALID_SOCKET;
sockaddr_in m_dest_addr;
bool m_winsock_initialized = false;

// Variables for calculating derivatives (acceleration)
float m_prev_velocity = 0.0f;
float m_prev_vert = 0.0f;
float m_prev_beta = 0.0f;
DWORD m_prev_time = 0;
DWORD m_last_log_time = 0;
bool m_first_run = true;

// Low-pass filter variables
float m_filtered_surge = 0.0f;
float m_filtered_sway = 0.0f;
float m_filtered_heave = 0.0f;
float m_filtered_roll = 0.0f;
float m_filtered_pitch = 0.0f;
float m_filtered_yaw = 0.0f;
float m_alpha = 0.3f; // smoothing factor

void log_debug(const char* message) {
	char log_path[MAX_PATH];
	GetModuleFileNameA(NULL, log_path, MAX_PATH);
	char* last_slash = strrchr(log_path, '\\');
	if (last_slash) *(last_slash + 1) = '\0';
	strcat(log_path, "telemetry_debug.txt");
	
	FILE* f = fopen(log_path, "a");
	if (f) {
		fprintf(f, "%lu: %s\n", GetTickCount(), message);
		fclose(f);
	}
}

extern "C" {
	__declspec(dllexport) void create_instrument(void* info);
	__declspec(dllexport) void delete_instrument();
	__declspec(dllexport) void pick_instrument(float dx, float dy);
	__declspec(dllexport) void move_instrument(float dx, float dy);
	__declspec(dllexport) void unpick_instrument();
	__declspec(dllexport) void update_instrument(HDC hdc);
	__declspec(dllexport) void update_instrument_ex(void* info, HDC hdc);
}

void create_instrument(void* info) {
	log_debug("create_instrument called");
	m_info = (panel_info*)info;

	// Initialize Winsock
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
		m_winsock_initialized = true;
		m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_socket != INVALID_SOCKET) {
			m_dest_addr.sin_family = AF_INET;
			m_dest_addr.sin_port = htons(4444); // Send to FlyPT Mover port 4444
			m_dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
			log_debug("Socket initialized successfully");
		} else {
			log_debug("Failed to create socket");
		}
	} else {
		log_debug("Failed to initialize Winsock");
	}

	m_prev_time = GetTickCount();
	m_last_log_time = m_prev_time;
	if (m_info) {
		if (m_info->velocity) m_prev_velocity = *m_info->velocity;
		if (m_info->vert) m_prev_vert = *m_info->vert;
		if (m_info->beta) m_prev_beta = *m_info->beta;
	}
	m_first_run = true;
}

void delete_instrument() {
	log_debug("delete_instrument called");
	if (m_socket != INVALID_SOCKET) {
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}
	if (m_winsock_initialized) {
		WSACleanup();
		m_winsock_initialized = false;
	}
}

void pick_instrument(float dx, float dy) {}
void move_instrument(float dx, float dy) {}
void unpick_instrument() {}

void update_instrument(HDC hdc) {
	if (!m_info) return;
	
	DWORD curr_time = GetTickCount();

	if (curr_time - m_last_log_time > 2000) { // Log every 2 seconds to avoid bloat
		char buff[256];
		sprintf(buff, "update_instrument - vel: %f, vert: %f, pitch: %f, roll: %f", 
			m_info->velocity ? *m_info->velocity : -999.0f,
			m_info->vert ? *m_info->vert : -999.0f,
			m_info->gama ? *m_info->gama : -999.0f,
			m_info->phi ? *m_info->phi : -999.0f);
		log_debug(buff);
		m_last_log_time = curr_time;
	}

	if (m_socket == INVALID_SOCKET) return;

	float pitch = m_info->gama ? (*m_info->gama) * 57.29578f : 0.0f;
	float roll = m_info->phi ? (*m_info->phi) * -57.29578f : 0.0f;
	float yaw = m_info->beta ? (*m_info->beta) * 57.29578f : 0.0f;

	float velocity = m_info->velocity ? *m_info->velocity : 0.0f;
	float vert = m_info->vert ? *m_info->vert : 0.0f;

	if (m_first_run) {
		m_prev_velocity = velocity;
		m_prev_vert = vert;
		if (m_info->beta) m_prev_beta = *m_info->beta;
		m_prev_time = curr_time;
		m_first_run = false;
		return;
	}

	float dt = (curr_time - m_prev_time) / 1000.0f;
	if (dt <= 0.001f) dt = 0.016f;
	if (dt > 0.1f) dt = 0.1f;

	float surge_accel = (velocity - m_prev_velocity) / dt;
	float heave_accel = (vert - m_prev_vert) / dt;

	float yaw_rad_s = m_info->beta ? (*m_info->beta - m_prev_beta) / dt : 0.0f;
	if (yaw_rad_s > 3.14159f / dt) yaw_rad_s -= 6.28318f / dt;
	if (yaw_rad_s < -3.14159f / dt) yaw_rad_s += 6.28318f / dt;

	float sway_accel = velocity * yaw_rad_s;

	m_prev_velocity = velocity;
	m_prev_vert = vert;
	if (m_info->beta) m_prev_beta = *m_info->beta;
	m_prev_time = curr_time;

	if (roll > 30.0f) roll = 30.0f;
	if (roll < -30.0f) roll = -30.0f;
	if (pitch > 30.0f) pitch = 30.0f;
	if (pitch < -30.0f) pitch = -30.0f;
	if (heave_accel > 2.0f) heave_accel = 2.0f;
	if (heave_accel < -2.0f) heave_accel = -2.0f;

	m_filtered_surge = m_alpha * surge_accel + (1.0f - m_alpha) * m_filtered_surge;
	m_filtered_sway = m_alpha * (-sway_accel) + (1.0f - m_alpha) * m_filtered_sway;
	m_filtered_heave = m_alpha * heave_accel + (1.0f - m_alpha) * m_filtered_heave;
	m_filtered_roll = m_alpha * roll + (1.0f - m_alpha) * m_filtered_roll;
	m_filtered_pitch = m_alpha * pitch + (1.0f - m_alpha) * m_filtered_pitch;
	m_filtered_yaw = m_alpha * yaw + (1.0f - m_alpha) * m_filtered_yaw;

	float packet[6];
	packet[0] = m_filtered_surge;
	packet[1] = m_filtered_sway;
	packet[2] = m_filtered_heave;
	packet[3] = m_filtered_roll;
	packet[4] = m_filtered_pitch;
	packet[5] = m_filtered_yaw;

	sendto(m_socket, (const char*)packet, sizeof(packet), 0, (struct sockaddr*)&m_dest_addr, sizeof(m_dest_addr));
}

void update_instrument_ex(void* info, HDC hdc) {
	m_info = (panel_info*)info;
	update_instrument(hdc);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
		log_debug("DLL_PROCESS_ATTACH");
	}
	return TRUE;
}
