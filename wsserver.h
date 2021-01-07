/*
 * wsserver.h <Hangzhou@astroair.cn>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */
 
/************************************************* 
 
Copyright: 2020-2021 Max Qian. All rights reserved
 
Author:Max Qian

E-mail:astro_air@126.com
 
Date:2021-1-8
 
Description:Main framework of astroair server
 
**************************************************/

#pragma once

#ifndef _WSSERVER_H_
#define _WSSERVER_H_

#include "config.h"

#ifdef HAS_WEBSOCKET
/*如果需要使用wss客户端*/
#ifdef HAS_OPENSSL
#include <websocketpp/config/asio.hpp>
#else
/*默认无加密客户端*/
#include <websocketpp/config/asio_no_tls.hpp>
#endif
#include <websocketpp/server.hpp>
#endif

#ifdef HAS_JSONCPP
#include <json/json.h>
#endif

#ifdef HAS_NOVA
#include "libastro.h"
#endif

#include <string>
#include <set>
#include <dirent.h>
#include <vector>
#include <atomic>
#include <fstream>

#ifdef HAS_WEBSOCKET
typedef websocketpp::server<websocketpp::config::asio> airserver;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;
typedef airserver::message_ptr message_ptr;
#endif

namespace AstroAir
{
	class WSSERVER
	{
		public:
			/*WebSocket服务器主体函数*/
			explicit WSSERVER();
			~WSSERVER();
			void on_open(websocketpp::connection_hdl hdl);
			void on_close(websocketpp::connection_hdl hdl);
			void on_message(websocketpp::connection_hdl hdl,message_ptr msg);
			void send(std::string payload);
			void send_binary(void const * payload, size_t len);
			void stop();
			virtual bool is_running();
			/*运行服务器*/
			void run(int port);
		public:
			virtual bool Connect(std::string Device_name);
			virtual bool Disconnect();
			virtual bool StartExposure(float exp,int bin,bool is_roi,int roi_type,int roi_x,int roi_y,bool is_save,std::string fitsname,int gain,int offset);
			virtual bool AbortExposure();
		protected:
			/*转化Json信息*/
			void readJson(std::string message);	
			/*WebSocket服务器功能性函数*/
			void SetDashBoardMode();
			void GetAstroAirProfiles();
			void SetupConnect(int timeout);
			/*处理正确返回信息*/
			void SetupConnectSuccess();
			void StartExposureSuccess();
			/*处理错误信息函数*/
			void SetupConnectError(std::string message);
			void StartExposureError(std::string message);
			void UnknownMsg();
			void UnknownDevice(int id,std::string message);
			void Polling();
		private:
			Json::Value root;
			Json::String errs;
			Json::CharReaderBuilder reader;
			std::string method,json_messenge;
			std::string Camera,Mount,Focus,Filter,Guide;
			std::string Camera_name,Mount_name,Focus_name,Filter_name,Guide_name;
			typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> con_list;
			con_list m_connections;
			airserver m_server;
			
			/*定义服务器设备参数*/
			WSSERVER *CCD,*MOUNT,*FOCUS,*FILTER,*GUIDE;

			/*服务器设备连接状态参数*/
			std::atomic_bool isConnected;
			std::atomic_bool isCameraConnected;
			std::atomic_bool isMountConnected;
			std::atomic_bool isFocusConnected;
			std::atomic_bool isFilterConnected;
			std::atomic_bool isGuideConnected;
	};
}

#endif
