#include <cmath>
#include <Eigen/Dense>
#include <boost/contract.hpp>
#include "bot.h"

// Abuse of macros.
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a ## b
#define Expects(x) boost::contract::check CONCAT(contract, __COUNTER__) = boost::contract::function().precondition([&] { BOOST_CONTRACT_ASSERT(x); })
#define Ensures(x) boost::contract::check CONCAT(contract, __COUNTER__) = boost::contract::function().postcondition([&] { BOOST_CONTRACT_ASSERT(x); })
#define sgn(x) (x > 0) - (x < 0)

void Bot::UDPRecv() {
    udp.Recv();
}

void Bot::UDPSend() {
    udp.Send();
}


// TODO: Prevent abuse of cmd.led and cmd.crc
bool Bot::validate_cmd(HighCmd cmd) {
    return
	cmd.levelFlag == 0x00 &&
	(cmd.mode == 1 || cmd.mode == 2) &&
	(cmd.forwardSpeed > -1 && cmd.forwardSpeed < 1) &&
	(cmd.sideSpeed > -1 && cmd.sideSpeed < 1) &&
	(cmd.rotateSpeed > -1 && cmd.rotateSpeed < 1) &&
	(cmd.bodyHeight > -1 && cmd.bodyHeight < 1) &&
	(cmd.yaw > -1 && cmd.yaw < 1) &&
	(cmd.pitch > -1 && cmd.pitch < 1) &&
	(cmd.roll > -1 && cmd.roll < 1);
}

void Bot::execute() {
    LoopFunc loop_control("control_loop", dt, boost::bind(&Bot::RobotControl, this));
    LoopFunc loop_udpSend("udp_send", dt, 3, boost::bind(&Bot::UDPSend, this));
    LoopFunc loop_udpRecv("udp_recv", dt, 3, boost::bind(&Bot::UDPRecv, this));

    loop_udpSend.start();
    loop_udpRecv.start();
    loop_control.start();

    while (true) {
	sleep(1);
    }
}

void Bot::RobotControl() {
    motiontime += 2;
    udp.GetRecv(state);
    InstructionOutput out;
    if (!executing) {
	out = instructions[index](state, state);
	initial_state = state;
	executing = true;
    } else {
	out = instructions[index](initial_state, state);
    }
    if (!out.done) {
	cmd = out.cmd;
    } else {
	executing = false;
	index++;
    }
    if (validate_cmd(cmd)) udp.SetSend(cmd);
}

void Bot::move_y(float d, float v) {
    Expects(v > -0.7 && v < 1);
    instructions.push_back(
	[d, v](HighState initial_state, HighState state) {
	    HighCmd cmd {0};
	    cmd.mode = 2;	    
	    (v < 0) ? cmd.forwardSpeed = v/0.7 : cmd.forwardSpeed = v;
	    return InstructionOutput{cmd, true};
	}
    );
}

void Bot::move_x(float d, float v) {
    Expects(v > -0.4 && v < 0.4);
    instructions.push_back(
	[d, v](HighState initial_state, HighState state) {
	    HighCmd cmd {0};
	    cmd.mode = 2;	    
	    cmd.sideSpeed = v;
	    return InstructionOutput{cmd, true};
	}
    );
}

void Bot::move(Eigen::Vector2d pos, Eigen::Vector2d v) {
    Expects((v[0] > -0.4 && v[0] < 0.4) && (v[1] > -0.7 && v[1] < 1));
    instructions.push_back(
	[pos, v](HighState initial_state, HighState state) {
	    HighCmd cmd {0};
	    cmd.mode = 2;	    
	    cmd.sideSpeed = v[0];
	    cmd.forwardSpeed = v[1];
	    return InstructionOutput{cmd, true};
	}
    );
}

void Bot::smooth_move(Eigen::Vector2d pos, float v, float omega) {
    Expects((v > -0.7 && v < 1) && (-2*pi/3 < omega && omega < 2*pi/3));
    float theta = atan(pos[1]/pos[0]);
    rotate(theta, sgn(theta)*omega);
    move_y(pos.norm(), v);
}

void Bot::set_led(std::vector<Eigen::Vector3i> lights) {
    Expects(lights.size() == 4);       
    instructions.push_back(
	[lights](HighState initial_state, HighState state) {
	    HighCmd cmd {0};
	    for (int i = 0; i < 4; i++) {
		// TODO avoid C-style cast.
		cmd.led[i] = *(LED*)lights[i].data();
	    }
	    return InstructionOutput{cmd, true};
	}
    );
}

Eigen::Vector3f pyr_from_quaternion(float* quaternion) {
    float qw = quaternion[0];
    float qx = quaternion[1];
    float qy = quaternion[2];
    float qz = quaternion[3];
    Eigen::Vector3f out;
    out << static_cast<float>(atan2(2.0*(qy*qz + qw*qx), qw*qw - qx*qx - qy*qy + qz*qz)),
	static_cast<float>(asin(-2.0*(qx*qz - qw*qy))),
	static_cast<float>(atan2(2.0*(qx*qy + qw*qz), qw*qw + qx*qx - qy*qy - qz*qz));
    return out;
}

void Bot::rotate(float theta, float omega) {
    Expects(-2*pi/3 < omega && omega < 2*pi/3);
    instructions.push_back(
	[theta, omega](HighState initial_state, HighState state) {
	    HighCmd cmd {0};
	    cmd.mode = 1; // Maybe?
	    cmd.rotateSpeed = omega / (2*pi/3);
	    Eigen::Vector3f init_theta = pyr_from_quaternion(initial_state.imu.quaternion);
	    Eigen::Vector3f cur_theta = pyr_from_quaternion(state.imu.quaternion);
	    return (cur_theta[0]-init_theta[0] == theta) ?
		InstructionOutput{cmd, true} : InstructionOutput{cmd, false};
	}
    );
}
