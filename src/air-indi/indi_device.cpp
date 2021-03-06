/*
 * indi_device.cpp
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

Date:2020-2-15

Description:INDI driver

**************************************************/

#include "indi_device.h"

namespace AstroAir
{
    INDICCD::INDICCD()
    {
        
    }

    INDICCD::~INDICCD()
    {
        
    }

    bool INDICCD::Connect(std::string Device_name)
    {
        indi_client->setServer("localhost", 7624);
        indi_client->watchDevice(Device_name.c_str());
        indi_client->connectServer();
        return true;
    }

    bool INDICCD::Disconnect()
    {
        return true;
    }
}