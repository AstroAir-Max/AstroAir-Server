/*
 * wsserver.cpp
 * 
 * Copyright (C) 2020-2021 Max Qian
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
 
/************************************************* 
 
Copyright: 2020-2021 Max Qian. All rights reserved
 
Author:Max Qian

E-mail:astro_air@126.com
 
Date:2021-2-14
 
Description:Main framework of astroair server

Using:Websocketpp<https://github.com/zaphoyd/websocketpp>
      
Using:JsonCpp<https://github.com/open-source-parsers/jsoncpp>

**************************************************/

#include "wsserver.h"
#include "logger.h"
#include "opencv.h"
#include "base64.h"

#ifdef HAS_ASI
    #include "air-asi/asi_ccd.h"
#endif
#ifdef HAS_QHY
    #include "air-qhy/qhy_ccd.h"
#endif
#ifdef HAS_INDI
    #include "air-indi/indi_device.h"
#endif

namespace AstroAir
{
#ifdef HAS_WEBSOCKET
    /*
     * name: WSSERVER()
     * describe: Constructor for initializing server parameters
     * 描述：构造函数，用于初始化服务器参数
     */
    WSSERVER::WSSERVER()
    {
        /*初始化WebSocket服务器*/
        /*加载设置*/
        m_server.clear_access_channels(websocketpp::log::alevel::all ^ websocketpp::log::alevel::frame_payload);
        m_server_tls.clear_access_channels(websocketpp::log::alevel::all ^ websocketpp::log::alevel::frame_payload);
        /*初始化服务器*/
        m_server.init_asio();
        m_server_tls.init_asio();
        /*设置重新使用端口*/
        m_server.set_reuse_addr(true);
        m_server_tls.set_reuse_addr(true);
        /*设置打开事件*/
        m_server.set_open_handler(bind(&WSSERVER::on_open, this , ::_1));
        m_server_tls.set_open_handler(bind(&WSSERVER::on_open_tls, this , ::_1));
        /*设置关闭事件*/
        m_server.set_close_handler(bind(&WSSERVER::on_close, this , ::_1));
        m_server_tls.set_close_handler(bind(&WSSERVER::on_close_tls, this , ::_1));
        /*设置事件*/
        m_server.set_message_handler(bind(&WSSERVER::on_message, this ,::_1,::_2));
        m_server_tls.set_message_handler(bind(&WSSERVER::on_message_tls,this,::_1,::_2));
        /*SSL设置*/
        m_server_tls.set_http_handler(bind(&WSSERVER::on_http,this,::_1));
        m_server_tls.set_tls_init_handler(bind(&WSSERVER::on_tls_init,this,MOZILLA_INTERMEDIATE,::_1));
        /*重置参数*/
        isConnected = false;            //客户端连接状态
        isCameraConnected = false;      //相机连接状态
        isMountConnected = false;       //赤道仪连接状态
        isFocusConnected = false;       //电动调焦座连接状态
        isFilterConnected = false;      //滤镜轮连接状态
        isGuideConnected = false;       //导星软件连接状态
    }
    
    /*
     * name: ~WSSERVER()
     * describe: Destructor
     * 描述：析构函数
     * calls: stop()
     */
    WSSERVER::~WSSERVER()
    {
		/*如果服务器正在工作，则在停止程序之前停止服务器*/
        if(isConnected ==true || isConnectedTLS == true)
        {
            stop();
        }
        delete [] CCD;
        delete [] MOUNT;
        delete [] FOCUS;
        delete [] FILTER;
        delete [] GUIDE;
    }

    /*
     * name: on_open(websocketpp::connection_hdl hdl)
     * @param hdl:WebSocket句柄
     * describe: Insert handle when server connects
     * 描述：服务器连接时插入句柄
     */
    void WSSERVER::on_open(websocketpp::connection_hdl hdl)
    {
        lock_guard<mutex> guard(mtx);
        airserver::connection_ptr con = m_server.get_con_from_hdl( hdl );      // 根据连接句柄获得连接对象
        std::string path = con->get_resource();
        IDLog("Successfully established connection with client path %s\n",path.c_str());
        m_connections.insert(hdl);
        isConnected = true;
        m_server_cond.notify_one();
    }
    
    /*
     * name: on_close(websocketpp::connection_hdl hdl)
     * @param hdl:WebSocket句柄
     * describe: Clear data on server disconnection
     * 描述：服务器断开连接时清空数据
     */
    void WSSERVER::on_close(websocketpp::connection_hdl hdl)
    {
        lock_guard<mutex> guard(mtx);
        IDLog("Disconnect from client\n");
        m_connections.erase(hdl);
        isConnected = false;
        m_server_cond.notify_one();
    }
    
    /*
     * name: on_open_tls(websocketpp::connection_hdl hdl)
     * @param hdl:WebSocket句柄
     * describe: Insert handle when server connects
     * 描述：服务器连接时插入句柄
     */
    void WSSERVER::on_open_tls(websocketpp::connection_hdl hdl)
    {
        lock_guard<mutex> guard(mtx);
        airserver_tls::connection_ptr con = m_server_tls.get_con_from_hdl( hdl );      // 根据连接句柄获得连接对象
        std::string path = con->get_resource();
        IDLog("Successfully established wss connection with client path %s\n",path.c_str());
        m_connections_tls.insert(hdl);
        isConnectedTLS = true;
        m_server_cond.notify_one();
    }
    
    /*
     * name: on_close_tls(websocketpp::connection_hdl hdl)
     * @param hdl:WebSocket句柄
     * describe: Clear data on server disconnection
     * 描述：服务器断开连接时清空数据
     */
    void WSSERVER::on_close_tls(websocketpp::connection_hdl hdl)
    {
        lock_guard<mutex> guard(mtx);
        IDLog("Disconnect from client\n");
        m_connections_tls.erase(hdl);
        isConnectedTLS = false;
        m_server_cond.notify_one();
    }

    /*
     * name: on_message(websocketpp::connection_hdl hdl,message_ptr msg)
     * @param hdl:WebSocket句柄
     * @param msg：服务器信息
     * describe: Processing information from clients
     * 描述：处理来自客户端的信息
     * calls: readJson(std::string message)
     */
    void WSSERVER::on_message(websocketpp::connection_hdl hdl,message_ptr msg)
    {
        std::string message = msg->get_payload();
        /*处理信息*/
        readJson(message);
    }
    
    /*
     * name: on_message_tls(websocketpp::connection_hdl hdl,message_ptr msg)
     * @param hdl:WebSocket句柄
     * @param msg：服务器信息
     * describe: Processing information from clients
     * 描述：处理来自客户端的信息
     * calls: readJson(std::string message)
     * note:This is the WSS server, please connect through the webpage of HTTPS
     */
    void WSSERVER::on_message_tls(websocketpp::connection_hdl hdl,message_ptr_tls msg)
    {
        std::string message = msg->get_payload();
        /*处理信息*/
        readJson(message);
    }

    /*
     * name: on_http(websocketpp::connection_hdl hdl) 
     * @param hdl:WebSocket句柄
     * describe: Get client information when client and server shake hands
     * 描述：在客户端与服务器握手时获取客户端信息
     * calls: set_body()
     * calls: set_status()
     * note:This is the WSS server, please connect through the webpage of HTTPS
     */
    void WSSERVER::on_http(websocketpp::connection_hdl hdl) 
    {
        lock_guard<mutex> guard(mtx_action);
        airserver_tls::connection_ptr con = m_server_tls.get_con_from_hdl(hdl);
        websocketpp::http::parser::request rt = con->get_request();
		const std::string& strUri = rt.get_uri();
		const std::string& strMethod = rt.get_method();
		const std::string& strBody = rt.get_body();	//只针对post时有数据
		const std::string& strHost = rt.get_header("host");
		const std::string& strContentType = rt.get_header("Content-type");
		const std::string& strVersion = rt.get_version();
        IDLog("Connection URI is %s,method is %s,host is %s",strUri.c_str(),strMethod.c_str(),strHost.c_str());
		websocketpp::http::parser::header_list listhtpp = rt.get_headers();
        con->set_body("Hello World!");
        con->set_status(websocketpp::http::status_code::ok);
        m_server_action.notify_one();
    }

    /*
     * name: get_password()
     * describe: Get password for client authentication
     * 描述：获取密码用于客户端验证
     * @return password:服务器密码
     */
    std::string WSSERVER::get_password() 
    {
        lock_guard<mutex> guard(mtx_action);
        std::string password;
        std::ifstream in("passwd.txt",std::ios::in);
        if (! in.is_open())
        {
            IDLog("Error opening password file,use default password\n");
            return "astroair";
        }
        in >> password;
        in.close();
        return password;
    }

    /*
     * name: on_tls_init(tls_mode mode, websocketpp::connection_hdl hdl)
     * @param hdl:WebSocket句柄
     * @param mode：加密类型
     * describe: Initialize the WSS connection and authenticate
     * 描述：初始化WSS连接，并进行身份验证
     */
    context_ptr_tls WSSERVER::on_tls_init(tls_mode mode, websocketpp::connection_hdl hdl)
    {
        lock_guard<mutex> guard(mtx_action);
        namespace asio = websocketpp::lib::asio;
        context_ptr_tls ctx = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);
        try
        {
            if (mode == MOZILLA_MODERN)
            {
                ctx->set_options(asio::ssl::context::default_workarounds |
                                asio::ssl::context::no_sslv2 |
                                asio::ssl::context::no_sslv3 |
                                asio::ssl::context::no_tlsv1 |
                                asio::ssl::context::single_dh_use);
            } 
            else
            {
                ctx->set_options(asio::ssl::context::default_workarounds |
                                asio::ssl::context::no_sslv2 |
                                asio::ssl::context::no_sslv3 |
                                asio::ssl::context::single_dh_use);
            }
            //ctx->set_password_callback(bind(&get_password));
            ctx->use_certificate_chain_file("server.crt");
            ctx->use_private_key_file("server.pem", asio::ssl::context::pem);
            ctx->use_tmp_dh_file("client.pem");
            std::string ciphers;
            if (mode == MOZILLA_MODERN)
                ciphers = "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!3DES:!MD5:!PSK";
            else
                ciphers = "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA:AES:CAMELLIA:DES-CBC3-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA";
            if (SSL_CTX_set_cipher_list(ctx->native_handle() , ciphers.c_str()) != 1)
            {
                std::cout << "Error setting cipher list" << std::endl;
            }
        } 
        catch (websocketpp::exception const &e)
        {
            std::cerr << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "other exception" << std::endl;
        }
        m_server_action.notify_one();
        return ctx;
    }

    /*以下三个函数均是用于switch支持string*/
	constexpr hash_t hash_compile_time(char const* str, hash_t last_value = basis)  
    {  
        return *str ? hash_compile_time(str+1, (*str ^ last_value) * prime) : last_value;  
    }  

    hash_t hash_(char const* str)  
    {  
        hash_t ret{basis};  
        while(*str){  
            ret ^= *str;  
            ret *= prime;  
            str++;  
        }  
        return ret;  
    }  
    
    constexpr unsigned long long operator "" _hash(char const* p, size_t)
    {
        return hash_compile_time(p);
    }
    
    /*
     * name: readJson(std::string message)
     * @param message:客户端信息
     * describe: Process information and complete
     * 描述：处理信息并完成对应任务
     * note: This is the heart of the whole process!!!
     */
    void WSSERVER::readJson(std::string message)
    {
        /*运用JsonCpp拆分JSON数组*/
        std::unique_ptr<Json::CharReader>const json_read(reader.newCharReader());
        json_read->parse(message.c_str(), message.c_str() + message.length(), &root,&errs);
        /*将string格式转化为const char*/
        method = root["method"].asString();
        const char* road = method.c_str();
        /*将接收到的信息写入文件
        #ifdef DEBUG_MODE
            if(method != "Polling")
                IDLog_CMDL(message.c_str());
        #endif
        */
        /*判断客户端需要执行的命令*/
        switch(hash_(road))
        {
            /*返回服务器版本号*/
            case "RemoteSetDashboardMode"_hash:
                SetDashBoardMode();
                break;
            /*返回当前目录下的文件*/
            case "RemoteGetAstroAirProfiles"_hash:
                GetAstroAirProfiles();
                break;
            /*连接设备*/
            case "RemoteSetupConnect"_hash:{
                std::thread ConnectThread(&WSSERVER::SetupConnect,this,root["params"]["TimeoutConnect"].asInt());
                ConnectThread.detach();
                break;
            }
            /*相机开始拍摄*/
            case "RemoteCameraShot"_hash:{
                std::thread CamThread(&WSSERVER::StartExposure,this,root["params"]["Expo"].asInt(),root["params"]["Bin"].asInt(),root["params"]["IsSaveFile"].asBool(),root["params"]["FitFileName"].asString(),root["params"]["Gain"].asInt(),root["params"]["Offset"].asInt());
                CamThread.detach();
                break;
            }
            /*相机停止拍摄*/
            case "RemoteActionAbort"_hash:
				AbortExposure();
				break;
            case "RemoteCooling"_hash:{
                std::thread CoolingThread(&WSSERVER::Cooling,this,root["IsSetPoint"].asBool(),root["IsCoolDown"].asBool(),root["IsASync"].asBool(),root["IsWarmup"].asBool(),root["IsCoolerOFF"].asBool());
                CoolingThread.detach();
                break;
            }
            /*轮询，保持连接*/
            case "Polling"_hash:
                Polling();
                break;
            /*默认返回未知信息*/
            default:
                UnknownMsg();
        }
    }
    
    /*
     * name: send(std::string payload)
     * @param message:需要发送的信息
     * describe: Send information to client both ws and wss
     * 描述：向ws和wss客户端发送信息
     * note: The message must be sent in the format of JSON
     */
    void WSSERVER::send(std::string message)
    {
        for (auto it : m_connections)
        {
            try
            {
                m_server.send(it, message, websocketpp::frame::opcode::text);
            }
            catch (websocketpp::exception const &e)
            {
                std::cerr << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "other exception" << std::endl;
            }
        }
        for (auto it : m_connections_tls)
        {
            try
            {
                m_server_tls.send(it, message, websocketpp::frame::opcode::text);
            }
            catch (websocketpp::exception const &e)
            {
                std::cerr << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "other exception" << std::endl;
            }
        }
    }
    
    /*
     * name: stop()
     * describe: Stop the websocket server
     * 描述：停止WebSocket服务器
	 * calls: IDLog(const char *fmt, ...)
     */
    void WSSERVER::stop()
    {
        for (auto it : m_connections)
        {
            m_server.close(it, websocketpp::close::status::normal, "Switched off by user.");
            m_server_tls.close(it, websocketpp::close::status::normal, "Switched off by user.");
        }
        IDLog("Stop the server..\n");
        m_connections.clear();
        m_connections_tls.clear();
        m_server.stop();
        m_server_tls.stop();
        IDLog("Good bye\n");
    }

    /*
     * name: is_running()
     * @return Boolean function:服务器运行状态
     *  -false: server is not running||服务器不在运行
     *  -true: server is running||服务器正在运行
     */
    bool WSSERVER:: is_running()
    {
        return m_server.is_listening();
    }
    
    /*
     * name: run(int port)
     * @param port:服务器端口
     * describe: This is used to start the websocket server
     * 描述：启动WebSocket服务器
	 * calls: IDLog(const char *fmt, ...)
     */
    void WSSERVER::run(int port)
    {
        try
        {
            IDLog("Start the server at port %d ...\n",port);
            m_server.listen(websocketpp::lib::asio::ip::tcp::v4(),port);
            m_server.start_accept();
            m_server.run();
        }
        catch (websocketpp::exception const & e)
        {   
			std::cerr << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "other exception" << std::endl;
        }
    }

    /*
     * name: run_tls(int port)
     * @param port:服务器端口
     * describe: This is used to start the wss websocket server
     * 描述：启动WebSocket服务器
	 * calls: IDLog(const char *fmt, ...)
     */
    void WSSERVER::run_tls(int port)
    {
        try
        {
            IDLog("Start the wss server at port %d ...\n",port);
            m_server_tls.listen(websocketpp::lib::asio::ip::tcp::v4(),port);
            m_server_tls.start_accept();
            m_server_tls.run();
        }
        catch (websocketpp::exception const & e)
        {   
			std::cerr << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "other exception" << std::endl;
        }
    }
#endif

    /*
     * name: SetDashBoardMode()
     * describe: This is used to initialize the connection and send the version number.
     * 描述：初始化连接，并发送版本号
     * calls: send(std::string message)
     */
    void WSSERVER::SetDashBoardMode()
	{
        Json::Value Root;
		Root["result"] = Json::Value(1);
		Root["code"] = Json::Value();
		Root["Event"] = Json::Value("Version");
		Root["AIRVersion"] = Json::Value("2.0.0");
		json_messenge = Root.toStyledString();
		send(json_messenge);
	}

    /*
     * name: GetAstroAirProfiles()
     * describe: Get the local file name and upload it to the client
     * 描述：获取本地文件名称并上传至客户端
     * describe: Gets the specified suffix file name in the folder
	 * 描述：获取文件夹中指定后缀文件名称
     * calls: send()
	 * note:The suffix of get file should be .air
     */
    void WSSERVER::GetAstroAirProfiles()
    {
        /*寻找当前文件夹下的所有文件*/
        struct dirent *ptr;      
		DIR *dir;  
		std::string PATH = "./";        //搜索目录，正式版本应该可以选择目录位置
		dir=opendir(PATH.c_str());      //打开目录
		std::vector<std::string> files;
        /*搜索所有符合条件的文件*/
		while((ptr = readdir(dir)) != NULL)  
		{  
			if(ptr->d_name[0] == '.' || strcmp(ptr->d_name,"..") == 0)  
				continue;
            /*判断文件后缀是否为.air*/
            int size = strlen(ptr->d_name);
            if(strcmp( ( ptr->d_name + (size - 4) ) , ".air") != 0)
                continue;
            files.push_back(ptr->d_name);
		}  
		closedir(dir);      //关闭目录
        /*判断是否找到配置文件*/
        if(files.begin() == files.end())
        {
            IDLog("Cound not found any configure files,please check it\n");
            //Max:这里还缺少一个如果没有文件的错误处理函数
            return;
        }
        /*整合信息并发送至客户端*/
        Json::Value Root;
		Root["Event"] = Json::Value("RemoteActionResult");
        Root["UID"] = Json::Value("RemoteGetAstroAirProfiles");
        Root["ActionResultInt"] = Json::Value(4);
        Root["ParamRet"]["list"]["name"] = Json::Value(files[0]);
        Root["ParamRet"]["FileNumber"] = Json::Value(files.size()-1);
        IDLog("Found configure file named %s\n",files[0].c_str());
        files.erase(files.begin());
        for (int i = 0; i < files.size(); i++)  
		{
            IDLog("Found configure file named %s\n",files[i].c_str());
            Root["ParamRet"]["Files"][i]["name"] = Json::Value(files[i]);
		}
        json_messenge = Root.toStyledString();
        send(json_messenge);
    }
    
    /*
     * name: SetupConnect(int timeout)
     * @param timeout:连接相机最长时间
     * describe: All connection profiles in the device
     * 描述：连接配置文件中的所有设备
     * calls: Connect(std::string Device_name)
     * calls: IDLog(const char *fmt, ...)
     * calls: IDLog_DEBUG(const char *fmt, ...)
     * calls: UnknownCamera()
     * calls: UnknownMount()
     * note: If it times out, an error message is returned
     */
    void WSSERVER::SetupConnect(int timeout)
    {
        /*读取config.air配置文件，并且存入参数中*/
        std::string line,jsonStr;
        std::ifstream in("config.air", std::ios::binary);
        /*打开文件*/
        if (!in.is_open())
        {
            IDLog("Unable to open configuration file\n");
            IDLog_DEBUG("Unable to open configuration file\n");
            return;
        }
        /*将文件转化为string格式*/
        while (getline(in, line))
        {
            jsonStr.append(line);
        }
        /*关闭文件*/
        in.close();
        /*将读取出的json数组转化为string*/
        std::unique_ptr<Json::CharReader>const json_read(reader.newCharReader());
        json_read->parse(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length(), &root,&errs);
        bool connect_ok = false;
        auto start = std::chrono::high_resolution_clock::now();
        /*连接指定品牌的指定型号相机*/
        bool camera_ok = false;
        bool Has_Camera = false;
        Camera = root["camera"]["brand"].asString();
        Camera_name = root["camera"]["name"].asString();
        if(!Camera.empty() && !Camera_name.empty())
        {
            Has_Camera = true;
            for(int i=1;i<=3;i++)
            {
                const char* a = Camera.c_str();
                switch(hash_(a))
                {
                    #ifdef HAS_ASI
                    {
                        #if HAS_ASI==ON
                            case "ZWOASI"_hash:{
                                /*初始化ASI相机，并赋值CCD*/
                                ASICCD *ASICamera = new ASICCD();
                                CCD = ASICamera;
                                camera_ok = CCD->Connect(Camera_name);
                                break;
                            }
                        #endif
                    }
                    #endif
                    #ifdef HAS_QHY
                    {
                        #if HAS_QHY==ON
                            case "QHYCCD"_hash:{
                                /*初始化QHY相机，并赋值CCD*/
                                QHYCCD *QHYCamera = new QHYCCD();
                                CCD = QHYCamera;
                                camera_ok = CCD->Connect(Camera_name);
                                break;
                            }
                        #endif
                    }
                    #endif
                    #ifdef HAS_INDI
                    {
                        #if HAS_INDI==ON
                            case "INDI"_hash:{
                                /*初始化INDI相机，并赋值CCD*/
                                INDICCD *INDIDevice = new INDICCD();
                                CCD = INDIDevice;
                                camera_ok = CCD->Connect(Camera_name);
                                break;
                            }
                        #endif
                    }
                    #endif
                    default:
                        UnknownDevice(301,"Unknown camera");		//未知相机返回错误信息
                }
                if(camera_ok == true)
                {
                    isCameraConnected = true;
                    break;
                }
                if(i == 3&& camera_ok == false)
                {
					/*相机未连接成功，返回错误信息*/
                    SetupConnectError(5);
                    break;
                }
                sleep(4);
            }
        }
        /*判断相机是否连接成功*/
        if(Has_Camera == true)
        {
			/*Max：这一段的使用逻辑尚需优化，关于如何判断是否所有设备都连接成功*/
            if(camera_ok == true)
                connect_ok = true;
            else
                connect_ok = false;
        }
        /*连接指定品牌的指定型号赤道仪*/
        bool mount_ok = false;		//赤道仪连接状态
        bool Has_Mount = false;		//是否拥有赤道仪
        Mount = root["mount"]["brand"].asString();
        Mount_name = root["mount"]["name"].asString();
        if(!Mount.empty() && !Mount_name.empty())
        {
            Has_Mount = true;
            for(int i=1;i<=3;i++)
            {
                const char* a = Mount.c_str();
                switch(hash_(a))
                {
					/*初始化iOptron赤道仪，并赋值MOUNT*/
                    #ifdef HAS_IOPTRON
                    case "iOptron"_hash:{
                        iOptron iOptronMount;
                        MOUNT = &iOptronMount;
                        monut_ok = MOUNT->Connect(Mount_name);
                        break;
                    }
                    #endif
                    #ifdef HAS_SKYWATCHER
                    case "SkyWatcher"_hash:{
						/*初始化SkyWatcher赤道仪，并赋值MOUNT*/
                        SkyWatcher SkyWatcherMount;
                        MOUNT = &SkyWatcherMount;
                        mount_ok = MOUNT->Connect(Mount_name);
                        break;
                    }
                    #endif
                    #ifdef HAS_INDI
                    case "INDIMount"_hash:{
						/*初始化INDI赤道仪，并赋值MOUNT*/
                        INDICCD *INDIDevice = new INDICCD();
                        MOUNT = INDIDevice;
                        mount_ok = MOUNT->Connect(Mount_name);
                        break;
                    }
                    #endif
                    default:
                        UnknownDevice(302,"Unknown mount");		//未知赤道仪返回错误信息
                }
                if(mount_ok == true)
                {
                    isMountConnected = true;
                    break;
                }
                if(i == 3&& mount_ok == false)
                {
					/*赤道仪未连接成功，返回错误信息*/
                    SetupConnectError(5);
                    break;
                }
                sleep(4);
            }
        }
        /*判断赤道仪是否连接成功*/
        if(Has_Mount == true)
        {
            if(mount_ok == true)
                connect_ok = true;
            else
                connect_ok = false;
        }
        /*连接指定品牌的指定型号电动调焦座*/
        bool focus_ok = false;
        bool Has_Focus = false;
        Focus = root["focus"]["brand"].asString();
        Focus_name = root["focus"]["name"].asString();
        if(!Focus.empty() && !Focus_name.empty())
        {
            Has_Focus = true;
            for(int i = 0;i<=3;i++)
            {
                const char* a = Focus.c_str();
                switch(hash_(a))
                {
                    #ifdef HAS_ASIEAF
                    case "ASIEAF"_hash:{
						/*初始化EAF电动调焦座，并赋FOCUS*/
                        FOCUS = &EAFFocus;
                        focus_ok = FOCUS->Connect(Focus_name);
                        break;
                    }
                    #endif
                    #ifdef HAS_GRUS
                    case "Grus"_hash:{
						/*初始化Grus电动调焦座，并赋FOCUS*/
                        FOCUS = &GRUSFocus;
                        focus_ok = FOCUS->Connect(Focus_name);
                        break;
                    }
                    #endif
                    #ifdef HAS_INDI
                    case "INDIFocus"_hash:{
						/*初始化INDI电动调焦座，并赋FOCUS*/
                        INDICCD *INDIDevice = new INDICCD();
                        FOCUS = INDIDevice;
                        focus_ok = FOCUS->Connect(Focus_name);
                        break;
                    }
                    #endif
                    default:
                        UnknownDevice(303,"Unknown focus");		//未知电动调焦座返回错误信息
                }
                if(focus_ok == true)
                {
                    isFocusConnected = true;
                    break;
                }
                if(i == 3&& focus_ok == false)
                {
					/*电动调焦座未连接成功，返回错误信息*/
                    SetupConnectError(5);
                    break;
                }
                sleep(4);
            }
        }
        /*判断电动调焦座是否连接成功*/
        if(Has_Focus == true)
        {
            if(focus_ok == true)
                connect_ok = true;
            else
                connect_ok = false;
        }
        /*连接指定品牌的指定型号滤镜轮*/
        bool filter_ok = false;
        bool Has_Filter = false;
        Filter = root["filter"]["brand"].asString();
        Filter_name = root["filter"]["name"].asString();
        if(!Filter.empty() && !Filter_name.empty())
        {
            Has_Filter = true;
            for(int i = 1;i<=3;i++)
            {
                const char* a = Filter.c_str();
                switch(hash_(a))
                {
                    #ifdef HAS_ASIEFW
                    case "ASIEFW"_hash:{
						/*初始化EFW滤镜轮，并赋FILTER*/
                        FILTER = &ASIFilter;
                        filter_ok = FILTER->Connect(Filter_name);
                        break;
                    }
                    #endif
                    #ifdef HAS_QHYCFW
                    case "QHYCFW"_hash:{
						/*初始化QHY滤镜轮，并赋FILTER*/
                        FILTER = &QHYFilter
                        filter_ok = FILTER->Connect(Filter_name);
                        break;
                    }
                    #endif
                    #ifdef HAS_INDI
                    case "INDIFilter"_hash:{
						/*初始化INDI滤镜轮，并赋FILTER*/
                        INDICCD *INDIDevice = new INDICCD();
                        FILTER = INDIDevice;
                        filter_ok = FILTER->Connect(Filter_name);
                        break; 
                    }
                    #endif
                    default:
                        UnknownDevice(304,"Unknown filter");		//未知滤镜轮返回错误信息
                }
                if(filter_ok == true)
                {
                    isFilterConnected = true;
                    break;
                }
                if(i == 3&& filter_ok == false)
                {
					/*滤镜轮未连接成功，返回错误信息*/
                    SetupConnectError(5);
                    break;
                }
                sleep(4);
            }
        }
        /*判断滤镜轮是否连接成功*/
        if(Has_Filter == true)
        {
            if(filter_ok == true)
                connect_ok = true;
            else
                connect_ok = false;
        }
        /*连接指定品牌的指定型号导星软件*/
        bool guide_ok = false;
        bool Has_Guide = false;
        Guide = root["Guide"]["brand"].asString();
        Guide_name = root["Guide"]["name"].asString();
        if(!Guide.empty() && !Guide_name.empty())
        {
            Has_Guide = true;
            for(int i = 1;i<=3;i++)
            {
                const char* a = Guide.c_str();
                switch(hash_(a))
                {
					/*Max：事实上我们一般只会使用PHD2，所以LinGuider可以等其他做好以后再做*/
                    #ifdef HAS_PHD2
                    case "PHD2"_hash:{
                        GUIDE = &PHD2;
                        guide_ok = GUIDE->Connect(Guide_name);
                        break;
                    }
                    #endif
                    #ifdef HAS_LINGUIDER
                    case "LinGuider"_hash:{
                        GUIDE = &LinGuider
                        guide_ok = GUIDE->Connect(Guide_name);
                        break;
                    }
                    #endif
                    default:
                        UnknownDevice(305,"Unknown guide server");		//未知导星软件返回错误信息
                }
                if(guide_ok == true)
                {
                    isGuideConnected = true;
                    break;
                }
                if(i == 3&& guide_ok == false)
                {
					/*导星软件未连接成功，返回错误信息*/
                    SetupConnectError(5);
                    break;
                }
                sleep(4);
            }
        }
        /*判断导星软件是否连接成功*/
        if(Has_Guide == true)
        {
            if(guide_ok == true)
                connect_ok = true;
            else
                connect_ok = false;
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;
        IDLog("Connecting to device took %g seconds\n", diff.count());
        if(diff.count() >= 20)
        {
            SetupConnectError(8);
            return;
        }
        /*判断设备是否完全连接成功*/
        if(connect_ok == true)
            SetupConnectSuccess();		//将连接上的设备列表发送给客户端
        else
            SetupConnectError(5);
        return;
    }
    
    /*
     * name: Connect(std::string Device_name)
     * @param Device_name:连接相机名称
     * describe: Connect the camera
     * 描述： 连接相机
     * calls: IDLog(const char *fmt, ...)
     * calls: IDLog_DEBUG(const char *fmt, ...)
	 * note:This function should not be executed normally
     */
    bool WSSERVER::Connect(std::string Device_name)
    {
		/*默认情况下不应该执行这个函数*/
        IDLog("Try to establish a connection with %s,Should never get here.\n",Device_name.c_str());
        IDLog_DEBUG("Try to establish a connection with %s,Should never get here.\n",Device_name.c_str());
        return true;
    }
    
    /*
     * name: Disconnect()
     * describe: Disconnect from camera
     * 描述：与相机断开连接
     * calls: IDLog(const char *fmt, ...)
     * calls: IDLog_DEBUG(const char *fmt, ...)
     * note: This function should not be executed normally
     */
    bool WSSERVER::Disconnect()
    {
		/*默认情况下不应该执行这个函数*/
        IDLog("Try to disconnect from %s,Should never get here.\n",Camera_name.c_str());
        IDLog_DEBUG("Try to disconnect from %s,Should never get here.\n",Camera_name.c_str());
        return true;
    }
    
    /*
     * name: StartExposure(int exp,int bin,bool is_roi,int roi_type,int roi_x,int roi_y,bool IsSave,std::string FitsName,int Gain,int Offset)
     * @param exp:相机曝光时间
     * @param bin:像素合并
     * @param IsSave:是否保存图像
     * @param FitsName:保存图像名称
     * @param Gain:相机增益
     * @param Offset:相机偏置
     * describe: Start exposure
     * 描述：开始曝光
	 * calls: StartExposure(int exp,int bin,bool IsSave,std::string FitsName,int Gain,int Offset)
     * calls: IDLog(const char *fmt, ...)
     * calls: IDLog_DEBUG(const char *fmt, ...)
	 * calls :StartExposureError(std::string message）
	 * note:This function should not be executed normally
     */
    bool WSSERVER::StartExposure(int exp,int bin,bool IsSave,std::string FitsName,int Gain,int Offset)
    {
		if(isCameraConnected == true)
		{
			bool camera_ok = false;
            Image_Name = FitsName;
			if ((camera_ok = CCD->StartExposure(exp, bin, IsSave, FitsName, Gain, Offset)) != true)
			{
				/*返回曝光错误的原因*/
				StartExposureError();
				IDLog("Unable to stop the exposure of the camera. Please check the connection of the camera. If you have any problems, please contact the developer\n");
				IDLog_DEBUG("Unable to stop the exposure of the camera. Please check the connection of the camera. If you have any problems, please contact the developer\n");
				/*如果函数执行不成功返回false*/
				return false;
			}
			/*将拍摄成功的消息返回至客户端*/
			StartExposureSuccess();
            newJPGReadySend();
		}
		else
		{
			IDLog("There seems to be some unknown mistakes here.Maybe you need to check the camera connection\n");
			IDLog_DEBUG("There seems to be some unknown mistakes here.Maybe you need to check the camera connection\n");
			return false;
        }
        return true;
    }
    
    /*
     * name: AbortExposure()
     * describe: Abort exposure
     * 描述：停止曝光
     * calls: IDLog(const char *fmt, ...)
     * calls: IDLog_DEBUG(const char *fmt, ...)
     * note: This function should not be executed normally
     */
    bool WSSERVER::AbortExposure()
    {
		if(isCameraConnected == true)
		{
			bool camera_ok = false;
			if ((camera_ok = CCD->AbortExposure()) != true)
			{
				/*返回曝光错误的原因*/
				AbortExposureError();
				IDLog("Unable to stop the exposure of the camera. Please check the connection of the camera. If you have any problems, please contact the developer\n");
				IDLog_DEBUG("Unable to stop the exposure of the camera. Please check the connection of the camera. If you have any problems, please contact the developer\n");
				/*如果函数执行不成功返回false*/
				return false;
			}
			/*将拍摄成功的消息返回至客户端*/
			AbortExposureSuccess();
		}
		else
		{
			IDLog("Try to stop exposure,Should never get here.\n");
			IDLog_DEBUG("Try to stop exposure,Should never get here.\n");
			return false;
        }
        return true;
    }
    
    bool WSSERVER::Cooling(bool SetPoint,bool CoolDown,bool ASync,bool Warmup,bool CoolerOFF)
    {
        return true;
    }

    /*
     * name: SetupConnectSuccess()
     * describe: Successfully connect device
     * 描述：成功连接设备
     * calls: IDLog(const char *fmt, ...)
     * calls: send()
     */
    void WSSERVER::SetupConnectSuccess()
    {
        IDLog("Successfully connect device\n");
        /*整合信息并发送至客户端*/
        Json::Value Root;
        Root["Event"] = Json::Value("RemoteActionResult");
        Root["UID"] = Json::Value("RemoteSetupConnect");
        Root["ActionResultInt"] = Json::Value(4);
        json_messenge = Root.toStyledString();
        send(json_messenge);
    }
    
    /*
     * name: SetupConnectError(int id)
     * describe: Error handling connection to device
     * 描述：处理连接设备时的错误
     * calls: IDLog(const char *fmt, ...)
     * calls: IDLog_DEBUG(const char *fmt, ...)
     * calls: send()
     */
    void WSSERVER::SetupConnectError(int id)
    {
        IDLog("Unable to connect device\n");
        IDLog_DEBUG("Unable to connect device\n");
        /*整合信息并发送至客户端*/
        Json::Value Root;
        Root["Event"] = Json::Value("RemoteActionResult");
        Root["UID"] = Json::Value("RemoteSetupConnect");
        Root["ActionResultInt"] = Json::Value(id);
        json_messenge = Root.toStyledString();
        send(json_messenge);
    }
    
    /*
     * name: StartExposureSuccess()
     * describe: Successfully exposure
     * 描述：成功连接设备
     * calls: IDLog(const char *fmt, ...)
     * calls: send()
     */
	void WSSERVER::StartExposureSuccess()
	{
        IDLog("Successfully exposure\n");
        /*整合信息并发送至客户端*/
        Json::Value Root;
        Root["Event"] = Json::Value("RemoteActionResult");
        Root["UID"] = Json::Value("RemoteCameraShot");
        Root["ActionResultInt"] = Json::Value(4);
        json_messenge = Root.toStyledString();
        send(json_messenge);
	}
	
    /*
     * name: AbortExposureSuccess()
     * describe: Successfully stop exposure
     * 描述：成功连接设备
     * calls: IDLog(const char *fmt, ...)
     * calls: send()
     */
	void WSSERVER::AbortExposureSuccess()
	{
		IDLog("Successfully stop exposure\n");
        /*整合信息并发送至客户端*/
        Json::Value Root;
        Root["Event"] = Json::Value("RemoteActionResult");
        Root["UID"] = Json::Value("RemoteCameraShot");
        Root["ActionResultInt"] = Json::Value(6);
        json_messenge = Root.toStyledString();
        send(json_messenge);
	}

	/*
	 * name: StartExposureError()
	 * describe: Error handling connection to device
	 * 描述：处理开始曝光时的错误
	 * calls: IDLog(const char *fmt, ...)
	 * calls: IDLog_DEBUG(const char *fmt, ...)
	 * calls: send()
	 */
    void WSSERVER::StartExposureError()
    {
		IDLog("Unable to start exposure\n");
		IDLog_DEBUG("Unable to start exposure\n");
		/*整合信息并发送至客户端*/
        Json::Value Root;
        Root["Event"] = Json::Value("RemoteActionResult");
        Root["UID"] = Json::Value("RemoteCameraShot");
        Root["ActionResultInt"] = Json::Value(5);
        json_messenge = Root.toStyledString();
		send(json_messenge);
    }
    
    /*
	 * name: AbortExposureError()
	 * describe: Unable to stop camera exposure
	 * 描述：无法停止相机曝光
	 * calls: IDLog(const char *fmt, ...)
	 * calls: IDLog_DEBUG(const char *fmt, ...)
	 * calls: send()
	 */
    void WSSERVER::AbortExposureError()
    {
		IDLog("Unable to stop camera exposure\n");
		IDLog_DEBUG("Unable to stop camera exposure\n");
		/*整合信息并发送至客户端*/
		Json::Value Root;
        Root["Event"] = Json::Value("RemoteActionResult");
        Root["UID"] = Json::Value("RemoteCameraShot");
        Root["ActionResultInt"] = Json::Value(5);
        json_messenge = Root.toStyledString();
		send(json_messenge);
    }
    
    /*
	 * name: newJPGReadySend()
	 * describe: Send the message that the picture is ready to the client
	 * 描述：将图片准备就绪的消息传给客户端
	 * calls: send()
     * calls: imread()
	 */
    void WSSERVER::newJPGReadySend()
    {
        /*读取JPG文件并转化为Mat格式*/
        const char* JPGName = strtok(const_cast<char *>(Image_Name.c_str()),".");
		strcat(const_cast<char *>(JPGName), ".jpg");
        cv::Mat ImageData = cv::imread(JPGName);
        /*组合即将发送的json信息*/
        Json::Value Root;
        Root["Event"] = Json::Value("NewJPGReady");
        Root["UID"] = Json::Value("RemoteCameraShot");
        Root["ActionResultInt"] = Json::Value(5);
        Root["Base64Data"] = Json::Value(JPGName);
        Root["PixelDimX"] = Json::Value(ImageData.rows);
        Root["PixelDimY"] = Json::Value(ImageData.cols);
        Root["SequenceTarget"] = Json::Value("");
        Root["Bin"] = Json::Value(1);
        Root["StarIndex"] = Json::Value(5);
        Root["HFD"] = Json::Value(1);
        Root["Expo"] = Json::Value(5);
        Root["TimeInfo"] = Json::Value(100);
        Root["Filter"] = Json::Value("** BayerMatrix **");
        json_messenge = Root.toStyledString();
        /*发送信息*/
		send(json_messenge);
    }

    /*
     * name: UnknownMsg()
     * describe: Processing unknown information from clients
     * 描述：处理来自客户端的未知信息
     * calls: IDLog(const char *fmt, ...)
     * calls: IDLog_DEBUG(const char *fmt, ...)
     * calls: send()
	 * note:If this function is executed, an error will appear on the web page
     */
    void WSSERVER::UnknownMsg()
    {
        IDLog("An unknown message was received from the client\n");
        IDLog_DEBUG("An unknown message was received from the client\n");
        /*整合信息并发送至客户端*/
        Json::Value Root,error;
        Root["result"] = Json::Value(1);
		Root["code"] = Json::Value();
        Root["id"] = Json::Value(403);
        error["message"] = Json::Value("Unknown information");
        Root["error"] = error;
        json_messenge = Root.toStyledString();
        send(json_messenge);
    }
    
    /*
     * name: UnknownDevice(int id,std::string message);
     * describe: Process the unknown device information and return to the client
     * 描述：处理未知设备信息，并返回至客户端
     * calls: IDLog(const char *fmt, ...)
     * calls: send()
	 * note:Execute this function if an unknown device is found
     */
    void WSSERVER::UnknownDevice(int id,std::string message)
    {
        IDLog("An unknown device was found,please check the connection\n");
        /*整合信息并发送至客户端*/
        Json::Value Root,error;
        Root["result"] = Json::Value(1);
		Root["code"] = Json::Value();
        Root["id"] = Json::Value(id);
        error["message"] = Json::Value(message);
        Root["error"] = error;
        json_messenge = Root.toStyledString();
        send(json_messenge);
    }
    
    void WSSERVER::ErrorCode()
    {
		
	}
	
    /*
     * name: Polling()
     * describe: The client remains connected to the server
     * 描述：客户端与服务器保持连接
     * calls: send()
	 * note:This function is most commonly used
     */
    void WSSERVER::Polling()
    {
        Json::Value Root;
        Root["result"] = Json::Value(1);
		Root["code"] = Json::Value();
        Root["Event"] = Json::Value("Polling");
        json_messenge = Root.toStyledString();
        send(json_messenge);
    }
        
}
