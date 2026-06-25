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
sockaddr_in m_dest_addr;      // FlyPT Mover (port 4444) - motion data for 6DoF
sockaddr_in m_simhub_addr;    // SimHub (port 4445) - instrument panel data
bool m_winsock_initialized = false;

// Command Listener Variables
SOCKET m_command_socket = INVALID_SOCKET;
sockaddr_in m_command_addr;
bool m_engine_failed = false;
bool m_rudder_failed = false;


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
			// FlyPT Mover - port 4444 (motion data for Thanos Controller / 6DoF hardware)
			m_dest_addr.sin_family = AF_INET;
			m_dest_addr.sin_port = htons(4444);
			m_dest_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

			// SimHub - port 4445 (instrument panel data)
			m_simhub_addr.sin_family = AF_INET;
			m_simhub_addr.sin_port = htons(4445);
			m_simhub_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

			log_debug("Socket initialized successfully (FlyPT:4444, SimHub:4445)");
		} else {
			log_debug("Failed to create socket");
		}

		// Initialize Command Listener Socket
		m_command_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (m_command_socket != INVALID_SOCKET) {
			m_command_addr.sin_family = AF_INET;
			m_command_addr.sin_port = htons(4446);
			m_command_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
			if (bind(m_command_socket, (struct sockaddr*)&m_command_addr, sizeof(m_command_addr)) == 0) {
				u_long mode = 1;
				ioctlsocket(m_command_socket, FIONBIO, &mode);
				log_debug("Command socket listening on 4446");
			} else {
				log_debug("Failed to bind command socket");
			}
		} else {
			log_debug("Failed to create command socket");
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
	if (m_command_socket != INVALID_SOCKET) {
		closesocket(m_command_socket);
		m_command_socket = INVALID_SOCKET;
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

	// Process incoming commands
	if (m_command_socket != INVALID_SOCKET) {
		char buffer[256];
		int recv_len = recvfrom(m_command_socket, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
		if (recv_len > 0) {
			buffer[recv_len] = '\0';
			if (strcmp(buffer, "CMD:ENGINE_FAIL") == 0) {
				m_engine_failed = true;
				log_debug("Command received: ENGINE_FAIL");
			} else if (strcmp(buffer, "CMD:ENGINE_RECOVER") == 0) {
				m_engine_failed = false;
				if (m_info->engine_on) *m_info->engine_on = true;
				if (m_info->stall) *m_info->stall = false;
				log_debug("Command received: ENGINE_RECOVER");
			} else if (strcmp(buffer, "CMD:RUDDER_FAIL") == 0) {
				m_rudder_failed = true;
				log_debug("Command received: RUDDER_FAIL");
			} else if (strcmp(buffer, "CMD:RUDDER_RECOVER") == 0) {
				m_rudder_failed = false;
				log_debug("Command received: RUDDER_RECOVER");
			}
		}
	}

	// Apply faults to the simulation state
	if (m_engine_failed) {
		if (m_info->engine_on) *m_info->engine_on = false;
		if (m_info->stall) *m_info->stall = true;
		if (m_info->motor_rpm) *m_info->motor_rpm = 0.0f;
		if (m_info->motor_rpm1) *m_info->motor_rpm1 = 0.0f;
		if (m_info->motor_rpm2) *m_info->motor_rpm2 = 0.0f;
		
		// Force control inputs to neutral/zero so the physics engine actually cuts power
		if (m_info->control_rpm) *m_info->control_rpm = 0.5f;
		if (m_info->control_rpm1) *m_info->control_rpm1 = 0.5f;
		if (m_info->control_rpm2) *m_info->control_rpm2 = 0.5f;
		if (m_info->control_mixture) *m_info->control_mixture = 0.0f;
	}
	if (m_rudder_failed) {
		if (m_info->control_delta) *m_info->control_delta = 0.0f;
	}
	
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

	// --- Kirim data motion ke FlyPT Mover (port 4444) untuk 6DoF / Thanos Controller ---
	sendto(m_socket, (const char*)packet, sizeof(packet), 0, (struct sockaddr*)&m_dest_addr, sizeof(m_dest_addr));

	// --- Kirim data instrumen ke SimHub (port 4445) ---
	// RPM: coba motor_rpm dulu, fallback ke rpm1 (twin engine seperti WHEC-STB)
	float rpm = 0.0f;
	if (m_info->motor_rpm  && *m_info->motor_rpm  > 0.0f) rpm = *m_info->motor_rpm;
	else if (m_info->motor_rpm1 && *m_info->motor_rpm1 > 0.0f) rpm = *m_info->motor_rpm1;
	else if (m_info->motor_rpm2 && *m_info->motor_rpm2 > 0.0f) rpm = *m_info->motor_rpm2;

	float spd_kts = velocity * 1.94384f; // Konversi m/s ke Knots
	float fuel    = m_info->fuel_left ? *m_info->fuel_left : 0.0f;

	// Heading: konversi beta (radian) ke derajat (0-360)
	float hdg_deg = m_info->beta ? (*m_info->beta * 57.29578f) : 0.0f;
	if (hdg_deg < 0.0f) hdg_deg += 360.0f;

	int eng_on = (m_info->engine_on && *m_info->engine_on) ? 1 : 0;
	int lgt_on = (m_info->light_on  && *m_info->light_on)  ? 1 : 0;
	int stall  = (m_info->stall     && *m_info->stall)     ? 1 : 0;

	// Format: Key:Value; - mudah dibaca oleh SimHub Custom UDP Plugin
	char sim_buf[256];
	int sim_len = sprintf(sim_buf,
		"RPM:%.1f;SPD:%.2f;FUEL:%.1f;HDG:%.1f;ENG:%d;LGT:%d;STALL:%d;ROLL:%.2f;PTCH:%.2f;HEAV:%.2f;",
		rpm, spd_kts, fuel, hdg_deg, eng_on, lgt_on, stall, roll, pitch, vert);

	// Log paket SimHub setiap 2 detik untuk debug
	if (curr_time - m_last_log_time < 100) { // hanya log di frame yg sama dgn velocity log
		log_debug(sim_buf);
	}

	if (sim_len > 0) {
		sendto(m_socket, sim_buf, sim_len, 0, (struct sockaddr*)&m_simhub_addr, sizeof(m_simhub_addr));
	}
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
