/*
 * ============================================================================
 *
 *  Zombie:Reloaded
 *
 *  File:          ztele.inc
 *  Type:          Module
 *  Description:   Player teleport manager.
 *
 *  Copyright (C) 2009-2010  Greyscale, Richard Helgeby
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ============================================================================
 */

// Include libraries.
#include "zr/libraries/conversionlib"

/**
 * This module's identifier.
 */
new Module:g_moduleZTele;

/**
 * Teleport events.
 */
new ProjectEvent:g_EvOnTeleportTimerStarted;
new ProjectEvent:g_EvOnPlayerTeleported;
new ProjectEvent:g_EvOnPlayerTeleportAborted;

/**
 * Teleport convars.
 */
new Handle:g_hCvarZTeleRandom;
new Handle:g_hCvarZTeleZombie;
new Handle:g_hCvarZTeleHumanBefore;
new Handle:g_hCvarZTeleHumanAfter;
new Handle:g_hCvarZTeleDelayZombie;
new Handle:g_hCvarZTeleDelayHuman;
new Handle:g_hCvarZTeleMaxZombie;
new Handle:g_hCvarZTeleMaxHuman;
new Handle:g_hCvarZTeleAutocancel;
new Handle:g_hCvarZTeleAutocancelDistance;

/**
 * A list of all used spawnpoints on the current map.
 */
new Float:g_vecZTeleSpawnPoints[MAXPLAYERS + 1][3];

/**
 * Current number of spawnpoints in the map.
 */
new g_iZTeleNumSpawnPoints;

/**
 * Location of client when they type !ztele.  This is used for the "auto-cancel" feature.
 */
new Float:g_vecZTeleOrigin[MAXPLAYERS + 1][3];

/**
 * Array to store the tele count of each client.
 */
new g_iZTeleCount[MAXPLAYERS + 1];

/**
 * Array for storing ZTele timer handles per client.
 */
new Handle:g_hZTeleTimer[MAXPLAYERS + 1];

/**
 * Array to store time left before teleport.
 */
new g_iZTeleTimeLeft[MAXPLAYERS + 1];

/**
 * Teleport abort reasons.
 */
enum ZTele_AbortReason
{
    ZTeleAbort_Manual,      /** Player manually aborted teleport. */
    ZTeleAbort_Autocancel   /** Teleport was aborted because of autocancel distance. */
}

/**
 * Register this module.
 */
ZTele_Register()
{
    // Define all the module's data as layed out by enum ModuleData in project.inc.
    new moduledata[ModuleData];
    
    moduledata[ModuleData_Disabled] = false;
    moduledata[ModuleData_Hidden] = false;
    strcopy(moduledata[ModuleData_FullName], CM_DATA_FULLNAME, "ZTele");
    strcopy(moduledata[ModuleData_ShortName], CM_DATA_SHORTNAME, "ztele");
    strcopy(moduledata[ModuleData_Description], CM_DATA_DESCRIPTION, "Player teleport manager.");
    moduledata[ModuleData_Dependencies][0] = INVALID_MODULE;
    
    // Send this array of data to the module manager.
    g_moduleZTele = ModuleMgr_Register(moduledata);
    
    EventMgr_RegisterEvent(g_moduleZTele, "Event_OnEventsRegister",         "ZTele_OnEventsRegister");
    
    // Create events.
    g_EvOnTeleportTimerStarted = EventMgr_CreateEvent("OnTeleportTimerStarted");
    g_EvOnPlayerTeleported = EventMgr_CreateEvent("OnPlayerTeleported");
    g_EvOnPlayerTeleportAborted = EventMgr_CreateEvent("OnPlayerTeleportAbort");
}

/**
 * Register all events here.
 */
public ZTele_OnEventsRegister()
{
    EventMgr_RegisterEvent(g_moduleZTele, "Event_OnMapStart",                   "ZTele_OnMapStart");
    EventMgr_RegisterEvent(g_moduleZTele, "Event_OnClientPutInServer",          "ZTele_OnClientPutInServer");
    EventMgr_RegisterEvent(g_moduleZTele, "Event_OnClientDisconnect",           "ZTele_OnClientDisconnect");
    EventMgr_RegisterEvent(g_moduleZTele, "Event_OnClientInfect",               "ZTele_OnClientInfect");
    EventMgr_RegisterEvent(g_moduleZTele, "Event_OnClientHuman",                "ZTele_OnClientHuman");
    
    #if defined PROJECT_GAME_CSS
    
    EventMgr_RegisterEvent(g_moduleZTele, "Event_PlayerSpawn",                  "ZTele_PlayerSpawn");
    EventMgr_RegisterEvent(g_moduleZTele, "Event_PlayerDeath",                  "ZTele_PlayerDeath");
    
    #endif
}

/**
 * Plugin is loading.
 */
ZTele_OnPluginStart()
{
    // Register the module.
    ZTele_Register();
    
    // Create cvars.
    g_hCvarZTeleRandom = Project_CreateConVar("ztele_random", "1", "Teleport back to a random spawnpoint on the map. ['0' = Teleports back to where you spawned during the round]");
    g_hCvarZTeleZombie = Project_CreateConVar("ztele_zombie", "1", "Allow zombies to use ZTele.");
    g_hCvarZTeleHumanBefore = Project_CreateConVar("ztele_human_before", "1", "Allow humans to use ZTele before the mother zombie has spawned.");
    g_hCvarZTeleHumanAfter = Project_CreateConVar("ztele_human_after", "1", "Allow humans to use ZTele after the mother zombie has spawned.");
    g_hCvarZTeleDelayZombie = Project_CreateConVar("ztele_delay_zombie", "3.0", "Time between using ZTele command and teleportation for zombies. [Dependency: <prefix>_ztele_zombie]");
    g_hCvarZTeleDelayHuman = Project_CreateConVar("ztele_delay_human", "3.0", "Time between using ZTele command and teleportation for humans. [Dependency: <prefix>_ztele_human_(before)/(after)]");
    g_hCvarZTeleMaxZombie = Project_CreateConVar("ztele_max_zombie", "3", "Max number of times a zombie is allowed to use ZTele per round. [Dependency: <prefix>_ztele_zombie]");
    g_hCvarZTeleMaxHuman = Project_CreateConVar("ztele_max_human", "1", "Max number of times a human is allowed to use ZTele per round. [Dependency: <prefix>_ztele_human_(before)/(after)]");
    g_hCvarZTeleAutocancel = Project_CreateConVar("ztele_autocancel", "1", "Automatically cancel ZTele if player moves out of the set boundary.");
    g_hCvarZTeleAutocancelDistance = Project_CreateConVar("ztele_autocancel_distance", "6.1m", "The radius the player can move without ZTele being auto-cancelled.  See top of file for supported units of measurement.");
    
    // Create commands
    RegConsoleCmd("ztele", ZTele_SayCommand, "Teleport to a known location by command.");
    Project_RegConsoleCmd("ztele", ZTele_Command, "ZTele a client. Usage: <prefix>_ztele <client>");
}

/**
 * The map has started.
 */
public ZTele_OnMapStart()
{
    // Reset spawn point data.
    g_iZTeleNumSpawnPoints = 0;
    for (new spindex = 0; spindex < sizeof(g_vecZTeleSpawnPoints); spindex++)
    {
        for (new i = 0; i < 3; i++)
            g_vecZTeleSpawnPoints[spindex][i] = 0.0;
    }
}

/**
 * Client connected to the server.
 * 
 * @param client    The client index.
 */
public ZTele_OnClientPutInServer(client)
{
    g_hZTeleTimer[client] = INVALID_HANDLE;
}

/**
 * Client is disconnecting from the server.
 * 
 * @param client    The client index.
 */
public ZTele_OnClientDisconnect(client)
{
    Util_CloseHandle(g_hZTeleTimer[client]);
}

/**
 * Client has spawned.
 * 
 * @param client    The client index.
 */
public ZTele_PlayerSpawn(client)
{
    // Check if the player is on a team. Spawning into the game is also a event in
    // the connection process where players get their observer camera.
    if (!IsPlayerAlive(client))
        return;
    
    // Check if the spawn point is in the array, if not then add it.
    new Float:vecLoc[3];
    GetClientAbsOrigin(client, vecLoc);
    
    // Check if we should find random spawn points or record the spawn point for a specific client.
    if (GetConVarBool(g_hCvarZTeleRandom))
    {
        if (Util_IsVecInArray(vecLoc, g_vecZTeleSpawnPoints, g_iZTeleNumSpawnPoints) == -1)
        {
            // Don't overflow the array.
            if (g_iZTeleNumSpawnPoints < sizeof(g_vecZTeleSpawnPoints))
            {
                // Copy into the spawnpoint array.
                for (new i = 0; i < 3; i++)
                    g_vecZTeleSpawnPoints[g_iZTeleNumSpawnPoints][i] = vecLoc[i];
                
                LogMgr_Print(g_moduleZTele, LogType_Normal, "Debug", "New ZTele point (%f, %f, %f) being added to array index %d.", vecLoc[0], vecLoc[1], vecLoc[2], g_iZTeleNumSpawnPoints);
                
                g_iZTeleNumSpawnPoints++;
            }
        }
    }
    else
    {
        // Copy the client's spawn point to teleport back to later during the round.
        for (new i = 0; i < 3; i++)
            g_vecZTeleSpawnPoints[client][i] = vecLoc[i];
    }
    
    g_iZTeleCount[client] = 0;
    Util_CloseHandle(g_hZTeleTimer[client]);
}

/**
 * Client has been killed.
 * 
 * @param victim    The index of the killed client.
 * @param attacker  The killer of the victim.
 * @param weapon    The weapon classname used to kill the victim. (No weapon_ prefix)
 * @param headshot  True if the death was by headshot, false if not.
 */
public ZTele_PlayerDeath(victim, attacker, const String:weapon[], bool:headshot)
{
    Util_CloseHandle(g_hZTeleTimer[victim]);
}

/**
 * Player has been infected.
 * 
 * @param client    The client index.
 */
public ZTele_OnClientInfect(client)
{
    g_iZTeleCount[client] = 0;
    Util_CloseHandle(g_hZTeleTimer[client]);
}

/**
 * Player has been turned back human.
 * 
 * @param client    The client index.
 */
public ZTele_OnClientHuman(client)
{
    g_iZTeleCount[client] = 0;
    Util_CloseHandle(g_hZTeleTimer[client]);
}

/**
 * Checks everything before starting the teleport sequence.
 * 
 * @param client    The client index.
 */
bool:ZTele_Client(client)
{
    // If the client is dead, then stop.
    if (!IsPlayerAlive(client))
    {
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Chat, _, _, "Must be alive");
        return false;
    }
    
    new bool:zombie = TLib_IsClientZombie(client);
    new bool:human = TLib_IsClientHuman(client);
    
    // If zombie cvar is disabled and the client is a zombie, then stop.
    new bool:ztelezombie = GetConVarBool(g_hCvarZTeleZombie);
    if (zombie && !ztelezombie)
    {
        // Tell client they must be human to use this feature.
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Chat, _, _, "Must be human");
        return false;
    }
    
    // If zombie has spawned, get before value, get the after value otherwise.
    // If the cvar is disabled and the client is a human, then stop.
    new bool:ztelehuman = ZRInfect_AreZombiesPresent() ? GetConVarBool(g_hCvarZTeleHumanAfter) : GetConVarBool(g_hCvarZTeleHumanBefore);
    if (human && !ztelehuman)
    {
        // Tell client that feature is restricted at this time.
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Chat, _, _, "ZTele restricted human");
        return false;
    }
    
    // If the tele limit has been reached, then stop.
    new ztelemax = zombie ? GetConVarInt(g_hCvarZTeleMaxZombie) : GetConVarInt(g_hCvarZTeleMaxHuman);
    if (g_iZTeleCount[client] >= ztelemax)
    {
        // Tell client that they have already reached their limit.
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Chat, _, _, "ZTele max", ztelemax);
        return false;
    }
    
    // If teleport is already in progress, then stop.
    if (g_hZTeleTimer[client] != INVALID_HANDLE)
    {
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Chat, _, _, "ZTele in progress");
        return false;
    }
    
    // Get the time the client has to wait before teleporting.
    new time = zombie ? GetConVarInt(g_hCvarZTeleDelayZombie) : GetConVarInt(g_hCvarZTeleDelayHuman);
    ZTele_StartTeleport(client, time);
    
    return true;
}

/**
 * Starts the teleport sequence.
 * 
 * @param client    The client index.
 * @param time      Time between calling this and actual teleportation.
 */
ZTele_StartTeleport(client, time)
{
    // Get current location.
    GetClientAbsOrigin(client, g_vecZTeleOrigin[client]);
    
    if (time > 0)
    {
        g_iZTeleTimeLeft[client] = time;
        
        // Tell client how much time is left until teleport.
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Center, _, _, "ZTele countdown", g_iZTeleTimeLeft[client]);
        g_hZTeleTimer[client] = CreateTimer(1.0, ZTele_Timer, client, TIMER_FLAG_NO_MAPCHANGE | TIMER_REPEAT);
        
        // Forward event to all modules.
        static EventDataTypes:eventdatatypes[] = {DataType_Cell, DataType_Cell};
        new any:eventdata[sizeof(eventdatatypes)][1];
        
        eventdata[0][0] = client;
        eventdata[1][0] = time;
        
        EventMgr_Forward(g_EvOnTeleportTimerStarted, eventdata, sizeof(eventdata), sizeof(eventdata[]), eventdatatypes);
    }
    else
    {
        ZTele_TeleportClient(client);
        
        new ztelemax = TLib_IsClientZombie(client) ? GetConVarInt(g_hCvarZTeleMaxZombie) : GetConVarInt(g_hCvarZTeleMaxHuman);
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Center, _, _, "ZTele countdown end", g_iZTeleCount[client], ztelemax);
    }
}

/**
 * Teleport client to a random spawn location.
 * 
 * @param client    The client index.
 */
ZTele_TeleportClient(client)
{
    // Verify that there is a point to teleport client to.
    if (g_iZTeleNumSpawnPoints <= 0)
    {
        LogMgr_Print(g_moduleZTele, LogType_Error, "ZTeleport", "No available point to teleport client %N to.", client);
        return;
    }
    
    // Check if we should teleport to a random point or teleport back to the client's spawn point.
    if (GetConVarBool(g_hCvarZTeleRandom))
    {
        // Find random spawn point.
        new rand_spindex = GetRandomInt(0, g_iZTeleNumSpawnPoints - 1);
        TeleportEntity(client, g_vecZTeleSpawnPoints[rand_spindex], NULL_VECTOR, Float:{0.0, 0.0, 0.0});
    }
    else
    {
        TeleportEntity(client, g_vecZTeleSpawnPoints[client], NULL_VECTOR, Float:{0.0, 0.0, 0.0});
    }
    
    // Forward event to all modules.
    new any:eventdata[1][1];
    eventdata[0][0] = client;
    
    EventMgr_Forward(g_EvOnPlayerTeleported, eventdata, sizeof(eventdata), sizeof(eventdata[]), g_CommonDataType1);
}

// **********************************************
//                  Callbacks
// **********************************************

/**
 * Menu callback (ztele_force)
 * Forces ZTele on a client.
 * 
 * @param menu      The menu handle.
 * @param action    Action client is doing in menu.
 * @param client    The client index.
 * @param slot      The menu slot selected. (starting from 0)
 */
public ZTele_ClientMenuHandle(Handle:menu_ztele, MenuAction:action, client, slot)
{
    // Client selected an option.
    if (action == MenuAction_Select)
    {
        // Get menu title.
        decl String:title[128];
        Format(title, sizeof(title), "%T", client, "ZTele menu clients title");
        
        // Get the client index of the selected client.
        new target = MenuLib_GetClientIndex(menu_ztele, slot);
            
        // If the target is 0, then the client left before being selected from the menu.
        if (target == 0)
        {
            // Re-send the menu.
            MenuLib_ClientMenu(client, ZTele_ClientMenuHandle, title, UTILS_FILTER_ALIVE | UTILS_FILTER_CANTARGET);
            
            return;
        }
        
        // Get the target's name for future use.
        decl String:targetname[MAX_NAME_LENGTH];
        GetClientName(target, targetname, sizeof(targetname));
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Reply, _, _, "ZTele command force successful", targetname);
        
        // Re-send the menu.
        MenuLib_ClientMenu(client, ZTele_ClientMenuHandle, title, UTILS_FILTER_ALIVE | UTILS_FILTER_CANTARGET);
    }
    // Client closed the menu.
    if (action == MenuAction_Cancel)
    {
        // Client hit "Back" button.
        if (slot == MenuCancel_ExitBack)
        {
            // Re-open admin menu.
            // ZAdminMenu(client);
        }
    }
    // Client exited menu.
    if (action == MenuAction_End)
    {
        CloseHandle(menu_ztele);
    }
}

/**
 * Command callback (ztele)
 * Teleport back to spawn if you are stuck.
 * 
 * @param client    The client index.
 * @param argc      Argument count.
 */
public Action:ZTele_SayCommand(client, argc)
{
    if (client == SERVER_INDEX)
    {
        TransMgr_PrintText(SERVER_INDEX, MsgFormat_Plugin, MsgType_Server, _, _, "Must be player");
        return Plugin_Handled;
    }
    
    // Start teleportation process.
    ZTele_Client(client);
    
    return Plugin_Handled;
}

/**
 * Command callback (zr_ztele)
 * Force ZSpawn on a client.
 * 
 * @param client    The client index.
 * @param argc      Argument count.
 */
public Action:ZTele_Command(client, argc)
{
    // Check if client has access.
    if (!AccessMgr_HasAccess(client, g_moduleZTele))
    {
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Reply, _, _, "No access to command");
        return Plugin_Handled;
    }
    
    // If not enough arguments given, then stop.
    if (argc < 1)
    {
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Reply, _, _, "ZTele command syntax");
        return Plugin_Handled;
    }
    
    decl String:target[MAX_NAME_LENGTH], String:targetname[MAX_NAME_LENGTH];
    new targets[MAXPLAYERS], bool:tn_is_ml, result;
    
    // Get targetname.
    GetCmdArg(1, target, sizeof(target));
    
    // Find a target.
    result = ProcessTargetString(target, client, targets, sizeof(targets), COMMAND_FILTER_ALIVE, targetname, sizeof(targetname), tn_is_ml);
        
    // Check if there was a problem finding a client.
    if (result <= 0)
    {
        TransMgr_ReplyToTargetError(client, result);
        return Plugin_Handled;
    }
    
    for (new tindex = 0; tindex < result; tindex++)
    {
        ZTele_StartTeleport(targets[tindex], 0);
        
        // Tell admin the outcome of the command if only 1 client was targetted.
        if (result == 1)
        {
            TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Reply, _, _, "ZTele command successful", targetname);
        }
        
        LogMgr_Print(g_moduleZTele, LogType_Normal, "ZTele Command", "\"%L\" teleported \"%L\" to spawn.", client, targets[tindex]);
    }
    
    return Plugin_Handled;
}

/**
 * Timer callback, counts down teleport to the client.
 * 
 * @param timer     The timer handle.
 * @param client    The client index.
 */
public Action:ZTele_Timer(Handle:timer, any:client)
{
    // Check if teleport should be auto-cancelled.
    if (GetConVarBool(g_hCvarZTeleAutocancel))
    {
        // If client has been running around after using ZTele, then stop timer.
        new Float:vecClient[3];
        GetClientAbsOrigin(client, vecClient);
        
        decl String:strDistance[16];
        GetConVarString(g_hCvarZTeleAutocancelDistance, strDistance, sizeof(strDistance));
        new Float:flDistance = ConvertLib_DetectAndConvert(strDistance, ConvertLibUnit_GameUnit, ConvertLibUnit_GameUnit);
        new Float:distance = GetVectorDistance(vecClient, g_vecZTeleOrigin[client]);
        
        // Check if distance has been surpassed.
        if (distance > flDistance)
        {
            // Reset timer handle.
            g_hZTeleTimer[client] = INVALID_HANDLE;
            
            // Tell client teleport has been cancelled.
            TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Center, _, _, "ZTele cancel");
            TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Chat, _, _, "ZTele autocancel", strDistance);
            
            // Forward event to all modules.
            static EventDataTypes:eventdatatypes[] = {DataType_Cell, DataType_Cell};
            new any:eventdata[sizeof(eventdatatypes)][1];
            
            eventdata[0][0] = client;
            eventdata[1][0] = _:ZTeleAbort_Autocancel;
            
            EventMgr_Forward(g_EvOnPlayerTeleportAborted, eventdata, sizeof(eventdata), sizeof(eventdata[]), eventdatatypes);
            
            // Stop timer.
            return Plugin_Stop;
        }
    }
    
    // Check if the time has expired.
    g_iZTeleTimeLeft[client]--;
    if (g_iZTeleTimeLeft[client] <= 0)
    {
        ZTele_TeleportClient(client);
        g_iZTeleCount[client]++;
        
        new ztelemax = TLib_IsClientZombie(client) ? GetConVarInt(g_hCvarZTeleMaxZombie) : GetConVarInt(g_hCvarZTeleMaxHuman);
        TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Center, _, _, "ZTele countdown end", g_iZTeleCount[client], ztelemax);
        
        // Stop repeating timer.
        g_hZTeleTimer[client] = INVALID_HANDLE;
        return Plugin_Stop;
    }
    TransMgr_PrintText(client, MsgFormat_Plugin, MsgType_Center, _, _, "ZTele countdown", g_iZTeleTimeLeft[client]);
    
    // Allow timer to continue repeating.
    return Plugin_Continue;
}