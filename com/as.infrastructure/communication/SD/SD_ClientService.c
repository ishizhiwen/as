/*-------------------------------- Arctic Core ------------------------------
 * Copyright (C) 2013, ArcCore AB, Sweden, www.arccore.com.
 * Contact: <contact@arccore.com>
 *
 * You may ONLY use this file:
 * 1)if you have a valid commercial ArcCore license and then in accordance with
 * the terms contained in the written license agreement between you and ArcCore,
 * or alternatively
 * 2)if you follow the terms found in GNU General Public License version 2 as
 * published by the Free Software Foundation and appearing in the file
 * LICENSE.GPL included in the packaging of this file or here
 * <http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>
 *-------------------------------- Arctic Core -----------------------------*/

/** @reqSettings DEFAULT_SPECIFICATION_REVISION=4.2.2 */

/** @req 4.2.2/SWS_SD_00029 The Service Discovery module shall only call SoAd_IfTransmit() if an IP address is assigned */

#include "SD.h"
#include "SD_Internal.h"

static const TcpIp_SockAddrType wildcard = {
        (TcpIp_DomainType) TCPIP_AF_INET,
        TCPIP_PORT_ANY,
        {TCPIP_IPADDR_ANY, TCPIP_IPADDR_ANY, TCPIP_IPADDR_ANY, TCPIP_IPADDR_ANY }
};

static Sd_Entry_Type1_Services entry11;
static Sd_Entry_Type2_EventGroups entry22;
static TcpIp_SockAddrType ipaddress;

static Sd_Message msg;
static uint8 *start_of_entries;

/** @req 4.2.2/SWS_SD_00485 */
static void EntryReceived(Sd_DynClientServiceType *client, Sd_Entry_Type1_Services **entry1, Sd_Entry_Type2_EventGroups **entry2, TcpIp_SockAddrType *ipaddress, boolean *is_multicast)
{
    Sd_InstanceType *server_svc;
    uint8 *option_run1 [MAX_OPTIONS] = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    uint8 *option_run2 [MAX_OPTIONS] = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    uint8 no_of_endpoints = 0;
    uint8 no_of_multicasts = 0;
    uint8 no_of_capabilty_records = 0;
    Ipv4Endpoint endpoint[MAX_OPTIONS];
    Ipv4Multicast multicast[MAX_OPTIONS];
    Sd_CapabilityRecordType capabilty_record[MAX_OPTIONS];

    /* First check if there are more entries in the last read message to fetch */
    if (msg.ProtocolVersion != 0x01) {
        /* Fetch a new message from the queue */
        if (!ReceiveSdMessage(&msg, ipaddress, CLIENT_QUEUE, &server_svc, is_multicast)) {
            *entry1 = NULL;
            *entry2 = NULL;
            return;
        } else
        {
        	start_of_entries = msg.EntriesArray;
        }

    }
    /* Find out which type of entry */
    uint8 type = msg.EntriesArray[0];

    if ((type == 0) || (type == 1)) {
        /* OFFER_SERVICE or STOP_OFFER_SERVICE */
        DecodeType1Entry (msg.EntriesArray, *entry1);
        *entry2 = NULL;
        if (msg.LengthOfOptionsArray > 0) {
            OptionsReceived(msg.OptionsArray, msg.LengthOfOptionsArray, *entry1, NULL, option_run1, option_run2);
        }
        /* Move entry pointer */
        msg.EntriesArray += ENTRY_TYPE_1_SIZE;
    }
    else if ((type == 6) || (type == 7)) {
        /* SUBSCRIBE_EVENTGROUP_ACK or SUBSCRIBE_EVENTGROUP_NACK */
        DecodeType2Entry (msg.EntriesArray, *entry2);
        *entry1 = NULL;
        if (msg.LengthOfOptionsArray > 0) {
            OptionsReceived(msg.OptionsArray, msg.LengthOfOptionsArray, NULL, *entry2, option_run1, option_run2);
        }
        /* Move entry pointer */
        msg.EntriesArray += ENTRY_TYPE_2_SIZE;
    }else
    {
        /** @req 4.2.2/SWS_SD_00483 */
        *entry1 = NULL;
        *entry2 = NULL;

		if ((msg.EntriesArray - start_of_entries) >= msg.LengthOfEntriesArray) {
            /* All entries are processed in this message.
             * Set protocolversion to 0 to indicate that a new message
             * should be fetched for the next entry. */
            msg.ProtocolVersion = 0x00;
        }

        return;
    }


    /* Decode and store the option parameters */
    /** @req 4.2.2/SWS_SD_00484 */
    DecodeOptionIpv4Endpoint(option_run1, endpoint, &no_of_endpoints);
    for (uint8 i=0; i < no_of_endpoints; i++){
       if (endpoint[i].Protocol == UDP_PROTO) {
           memcpy(&client->UdpEndpoint, &endpoint[i], sizeof(Ipv4Endpoint));
           client->UdpEndpoint.valid = TRUE;
       }
       else if (endpoint[i].Protocol == TCP_PROTO) {
           memcpy(&client->TcpEndpoint, &endpoint[i], sizeof(Ipv4Endpoint));
           client->TcpEndpoint.valid = TRUE;
       }
    }

    DecodeOptionIpv4Multicast(option_run1, multicast, &no_of_multicasts);
    for (uint8 i=0; i < no_of_multicasts; i++){
        for (uint8 eg=0; eg < client->ClientServiceCfg->NoOfConsumedEventGroups;eg++) {
            memcpy(&client->ConsumedEventGroups[eg].MulticastAddress, &multicast[i], MAX_OPTIONS);
            client->ConsumedEventGroups[eg].MulticastAddress.valid = TRUE;
        }
    }

    /* Decode configuration option attribute */
    DecodeOptionConfiguration(option_run1,capabilty_record,&no_of_capabilty_records);

    /* Check if this message is aimed for this client instance */
    if (*entry1 != NULL) {
        /** @req 4.2.2/SWS_SD_00487 */
        /* Received OfferService or StopOfferService */
        if (((*entry1)->ServiceID == client->ClientServiceCfg->Id) &&
            ((*entry1)->InstanceID == client->ClientServiceCfg->InstanceId) &&
            ((*entry1)->MajorVersion == client->ClientServiceCfg->MajorVersion) &&
            /** @req 4.2.2/SWS_SD_00488 */
            /** @req 4.2.2/SWS_SD_00489 */
            /** @req 4.2.2/SWS_SD_00482 */
            /* NOTE! wildcard for MinorVersion according to 4.2.2/SWS_SD_00488 should be 0xFFFFFF.  Since it is 32 bits we assume 0xFFFFFFFF. Correct?*/
            ((client->ClientServiceCfg->MinorVersion == 0xFFFFFFFF) || ((*entry1)->MinorVersion == client->ClientServiceCfg->MinorVersion))) {
                /* This client is the intended one. Retrieve remote ipaddress. If endpoint option is set use this instead */
            if (client->TcpEndpoint.valid != FALSE) {
                   ipaddress->addr[0] = ((client->TcpEndpoint.IPv4Address & 0xFF000000) >> 24);
                   ipaddress->addr[1] = ((client->TcpEndpoint.IPv4Address & 0x00FF0000) >> 16);
                   ipaddress->addr[2] = ((client->TcpEndpoint.IPv4Address & 0x0000FF00) >> 8);
                   ipaddress->addr[3] = (client->TcpEndpoint.IPv4Address & 0x000000FF);
                   ipaddress->domain = TCPIP_AF_INET;
                   ipaddress->port = client->TcpEndpoint.PortNumber;
            } else if (client->UdpEndpoint.valid != FALSE) {
                   ipaddress->addr[0] = ((client->UdpEndpoint.IPv4Address & 0xFF000000) >> 24);
                   ipaddress->addr[1] = ((client->UdpEndpoint.IPv4Address & 0x00FF0000) >> 16);
                   ipaddress->addr[2] = ((client->UdpEndpoint.IPv4Address & 0x0000FF00) >> 8);
                   ipaddress->addr[3] = (client->UdpEndpoint.IPv4Address & 0x000000FF);
                   ipaddress->domain = TCPIP_AF_INET;
                   ipaddress->port = client->UdpEndpoint.PortNumber;
            }
            else
            {
                SoAd_SoConIdType server_socket = Sd_DynConfig.Instance->MulticastRxSoCon;
                (void)SoAd_GetRemoteAddr(server_socket, ipaddress);
                (void)SoAd_SetRemoteAddr(Sd_DynConfig.Instance->TxSoCon, &wildcard);
            }
            FreeSdMessage(CLIENT_QUEUE);
        }
        else
        {
            *entry1 = NULL;
            *entry2 = NULL;
        }

    } else if (entry2 != NULL) {
        /* Received SubscribeEventgroupAck or SubscribeEventgroupNack */
        /** @req 4.2.2/SWS_SD_00490 */
        boolean EventGroupFound = FALSE;
        for (uint8 i=0; i < client->ClientServiceCfg->NoOfConsumedEventGroups; i++){
            if (((*entry2)->ServiceID == client->ClientServiceCfg->Id) &&
                ((*entry2)->InstanceID == client->ClientServiceCfg->InstanceId) &&
                ((*entry2)->MajorVersion == client->ClientServiceCfg->MajorVersion) &&
                ((*entry2)->EventgroupID == client->ClientServiceCfg->ConsumedEventGroup[i].Id))
            {
                /* SoAd_RequestIpAddrAssignment is called in statemachine,
                 * so here we only check if the entry was aimed for this client instance. */
                EventGroupFound = TRUE;
                break;
            }
        }

        if (!EventGroupFound)
        {
            *entry1 = NULL;
            *entry2 = NULL;
        }else{
            FreeSdMessage(CLIENT_QUEUE);
        }
    }
	if ((msg.EntriesArray - start_of_entries) >= msg.LengthOfEntriesArray) {
        /* All entries are processed in this message.
         * Set protcolversion to 0 to indicate that a new message
         * should be fetched for the next entry. */
        msg.ProtocolVersion = 0x00;
    }

}


/* Returns  a socket id defined for this client */
SoAd_SoConIdType GetSocket (Sd_DynClientServiceType const *client)
{
    SoAd_SoConIdType socket = 0;

    if (client->ClientServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr != NULL) {
        socket = client->ClientServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr[0];
    }
    else if (client->ClientServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr != NULL)
    {
        socket = client->ClientServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr[0];
    }
    return socket;
}

/** @req 4.2.2/SWS_SD_00419 */
/* Set the remote address for socket used by the methods, if endpoint options are received */
static void SetMethodsRemoteAddress(Sd_DynClientServiceType *client) {

    TcpIp_SockAddrType remote_address;

    if (client->ClientServiceCfg->UdpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET)
    {
        if (client->UdpEndpoint.valid != FALSE) {
           remote_address.addr[0] = ((client->UdpEndpoint.IPv4Address & 0xFF000000) >> 24);
           remote_address.addr[1] = ((client->UdpEndpoint.IPv4Address & 0x00FF0000) >> 16);
           remote_address.addr[2] = ((client->UdpEndpoint.IPv4Address & 0x0000FF00) >> 8);
           remote_address.addr[3] = (client->UdpEndpoint.IPv4Address & 0x000000FF);
           remote_address.domain = TCPIP_AF_INET;
           remote_address.port = client->UdpEndpoint.PortNumber;

           (void)SoAd_SetRemoteAddr(GetSocket(client), &remote_address);
        }
    } else if (client->ClientServiceCfg->TcpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET)
    {
        if (client->TcpEndpoint.valid != FALSE) {
           remote_address.addr[0] = ((client->TcpEndpoint.IPv4Address & 0xFF000000) >> 24);
           remote_address.addr[1] = ((client->TcpEndpoint.IPv4Address & 0x00FF0000) >> 16);
           remote_address.addr[2] = ((client->TcpEndpoint.IPv4Address & 0x0000FF00) >> 8);
           remote_address.addr[3] = (client->TcpEndpoint.IPv4Address & 0x000000FF);
           remote_address.domain = TCPIP_AF_INET;
           remote_address.port = client->TcpEndpoint.PortNumber;

           (void)SoAd_SetRemoteAddr(GetSocket(client), &remote_address);
        }
    }
}

/* Open TCP Connection if TcpRef is configured and was not opened before*/
static void OpenTcpConnection(Sd_DynClientServiceType *client)
{
    if ((client->ClientServiceCfg->TcpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET) && !(client->TcpSoConOpened))
    {
        /* Open all SoCons in Group */
        for (uint16 i=0;i<sizeof(client->ClientServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr)/sizeof(uint16); i++){
            (void)SoAd_OpenSoCon(client->ClientServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr[i]);
        }
        client->TcpSoConOpened = TRUE;
    }
}

/* Open Udp Connection if UdpRef is configured and was not opened before*/
static void OpenUdpConnection(Sd_DynClientServiceType *client)
{
    if ((client->ClientServiceCfg->UdpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET) && !(client->UdpSoConOpened))
    {
        /* Open all SoCons in Group */
        for (uint16 i=0;i<sizeof(client->ClientServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr)/sizeof(uint16); i++){
            (void)SoAd_OpenSoCon(client->ClientServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr[i]);
        }
        client->UdpSoConOpened = TRUE;
    }
}

static void OpenSocketConnections(Sd_DynClientServiceType *client)
{
    OpenTcpConnection(client);
    OpenUdpConnection(client);
    client->SocketConnectionOpened = TRUE;
}

/* Close TCP Connection if TcpRef is configured and was not opened before*/
static void CloseTcpConnection(Sd_DynClientServiceType *client, boolean do_abort)
{
    if ((client->ClientServiceCfg->TcpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET) && (client->TcpSoConOpened)) {
        /* Open all SoCons in Group */
        for (uint16 i=0;i<sizeof(client->ClientServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr)/sizeof(uint16); i++) {
            (void)SoAd_CloseSoCon(client->ClientServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr[i], do_abort);
        }

        client->TcpSoConOpened = FALSE;
    }
}

/* Close Udp Connection if UdpRef is configured and was not opened before*/
static void CloseUdpConnection(Sd_DynClientServiceType *client, boolean do_abort)
{
    if ((client->ClientServiceCfg->UdpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET) && (client->UdpSoConOpened)) {
        for (uint16 i=0;i<sizeof(client->ClientServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr)/sizeof(uint16); i++) {
            (void)SoAd_CloseSoCon(client->ClientServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr[i], do_abort);
        }
        client->UdpSoConOpened = FALSE;
    }
}

static void CloseSocketConnections(Sd_DynClientServiceType *client, boolean do_abort)
{
    CloseTcpConnection(client, do_abort);
    CloseUdpConnection(client, do_abort);
    client->SocketConnectionOpened = FALSE;
}

/* ClientService State Machine */
void UpdateClientService(const Sd_ConfigType *cfgPtr, uint32 instanceno, uint32 clientno) {
    Sd_DynClientServiceType *client = &Sd_DynConfig.Instance[instanceno].SdClientService[clientno];
    Sd_DynInstanceType *sd_instance = &Sd_DynConfig.Instance[instanceno];
    Sd_Entry_Type1_Services *entry1 = &entry11;
    Sd_Entry_Type2_EventGroups *entry2 = &entry22;
    boolean is_multicast = FALSE;

    memset(&entry11, 0, sizeof(Sd_Entry_Type1_Services));
    memset(&entry22, 0, sizeof(Sd_Entry_Type2_EventGroups));

    /* Fetch next entry and options, if any, from the queue */
    EntryReceived(client, &entry1, &entry2, &ipaddress,&is_multicast);

    switch (client->Phase)
    {
    case SD_DOWN_PHASE:

#if 0
        /* CORE-2261 addresses this */
        /** @req 4.2.2/SWS_SD_00350 */
        if (client->ClientServiceCfg->AutoRequire) {
            client->ClientServiceMode = SD_CLIENT_SERVICE_REQUESTED;
        }
#endif
        /** @req 4.2.2/SWS_SD_00463 */
        if (entry1 != (Sd_Entry_Type1_Services *) NULL) {
            if ((entry1->Type == STOP_OFFER_SERVICE_TYPE) && (entry1->TTL == 0)) {
                client->OfferActive = FALSE;
            }
        }

        if (client->TTL_Timer_Value_ms > 0) {
            if (client->OfferActive) {
                client->CurrentState = SD_CLIENT_SERVICE_AVAILABLE;
            }
            else {
                client->CurrentState = SD_CLIENT_SERVICE_DOWN;

                if (entry1 != (Sd_Entry_Type1_Services *) NULL) {
                    if ((entry1->Type == OFFER_SERVICE_TYPE) && (entry1->TTL > 0)) {
                        /* Reset TTL timer */
                        client->TTL_Timer_Value_ms = entry1->TTL * 1000u;  /* Value in secs */
                        client->TTL_Timer_Running = TRUE;
                        client->OfferActive = TRUE;
                        client->CurrentState = SD_CLIENT_SERVICE_AVAILABLE;
                    }
                }

            }

            /** @req 4.2.2/SWS_SD_00462 */
            /** @req 4.2.2/SWS_SD_00464 */
            if (client->ClientServiceMode == SD_CLIENT_SERVICE_REQUESTED) {

                /* Open TCP Connection if TcpRef is configured and
                 * was not opened before*/
                OpenTcpConnection(client);

                client->TTL_Timer_Value_ms = client->ClientServiceCfg->TimerRef->TTL * 1000u;  /* TBD: No req so not sure if it should be done here */
                client->TTL_Timer_Running = TRUE;
                client->Phase = SD_MAIN_PHASE;  /* DOWN -> MAIN */
                break;
            }

            if (client->TTL_Timer_Running) {
                client->TTL_Timer_Value_ms -=  SD_MAIN_FUNCTION_CYCLE_TIME_MS;
            }

        }else
        {
            client->CurrentState = SD_CLIENT_SERVICE_DOWN;

            /** @req 4.2.2/SWS_SD_00463 */
            if (entry1 != (Sd_Entry_Type1_Services *) NULL){
                if ((entry1->Type == OFFER_SERVICE_TYPE) && (entry1->TTL > 0)) {
                    /* Start TTL timer */
                    client->TTL_Timer_Value_ms = entry1->TTL * 1000u;
                    client->TTL_Timer_Running = TRUE;
                    client->OfferActive = TRUE;
                    client->CurrentState = SD_CLIENT_SERVICE_AVAILABLE;

                    /** @req 4.2.2/SWS_SD_00464 */
                    if (client->ClientServiceMode == SD_CLIENT_SERVICE_REQUESTED) {

                        /* Open TCP Connection if TcpRef is configured and
                         * was not opened before*/
                        OpenTcpConnection(client);

                        client->Phase = SD_MAIN_PHASE;  /* DOWN -> MAIN */
                        break;
                    }
                }
            }


            /** @req 4.2.2/SWS_SD_00350 */
            if ((client->ClientServiceMode == SD_CLIENT_SERVICE_REQUESTED) && (sd_instance->TxPduIpAddressAssigned)) {

                /** @req 4.2.2/SWS_SD_00362 */
                /* Call SoAd_EnableSpecificRouting():*/
                (void)SoAd_EnableSpecificRouting (client->ClientServiceCfg->ConsumedMethods.ClientServiceActivationRef,
                        client->ClientServiceCfg->UdpSocketConnectionGroupId);

                client->Phase = SD_INITIAL_WAIT_PHASE; /* DOWN -> INITIAL_WAIT */
                client->TTL_Timer_Running = FALSE; /* TBD: Don't run TTL Timer in Initial Phase? */
                break;
            }

        }

        break;
    case SD_INITIAL_WAIT_PHASE:

        /** @req 4.2.2/SWS_SD_00355 */
        if (client->ClientServiceMode == SD_CLIENT_SERVICE_RELEASED) {
            /** @req 4.2.2/SWS_SD_00456 */
            client->FindDelay_Timer_Value_ms = 0;
            client->FindDelayTimerOn = FALSE;
            client->Phase = SD_DOWN_PHASE; /* INITIAL_WAIT -> DOWN */
            client->TTL_Timer_Running = FALSE;

            /** @req 4.2.2/SWS_SD_00603 */
            if (client->SocketConnectionOpened){
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this client service instance */
                CloseSocketConnections(client, TRUE);  // IMPROVEMENT: Investigate abort parameter
            }
            client->CurrentState = SD_CLIENT_SERVICE_DOWN;
            break;
        }

        /** @req 4.2.2/SWS_SD_00357 */
        /** @req 4.2.2/SWS_SD_00354 */
        if ((!sd_instance->TxPduIpAddressAssigned) || (sd_instance->SdInitCalled)) {
            /** @req 4.2.2/SWS_SD_00456 */
            client->FindDelay_Timer_Value_ms = 0;
            client->FindDelayTimerOn = FALSE;
            client->Phase = SD_DOWN_PHASE; /* INITIAL_WAIT -> DOWN */
            client->TTL_Timer_Running = FALSE;

            /** @req 4.2.2/SWS_SD_00603 */
            if (client->SocketConnectionOpened){
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this client service instance */
                CloseSocketConnections(client, TRUE);  // IMPROVEMENT: Investigate abort parameter
            }

            /* Set all ConsumedEventGroupCurrentState to DOWN */
            for (uint32 eg = 0; eg < client->ClientServiceCfg->NoOfConsumedEventGroups; eg++){
                BswM_Sd_ConsumedEventGroupCurrentState(
                        client->ClientServiceCfg->ConsumedEventGroup[eg].HandleId,
                        SD_CONSUMED_EVENTGROUP_DOWN);
            }
            client->CurrentState = SD_CLIENT_SERVICE_DOWN;
        }


        if (!client->FindDelayTimerOn) {
            /** @req 4.2.2/SWS_SD_00351 */
            /* Start random FindDelay Timer */
            client->FindDelay_Timer_Value_ms = RandomDelay(client->ClientServiceCfg->TimerRef->InitialFindDelayMin_ms,
                    client->ClientServiceCfg->TimerRef->InitialFindDelayMax_ms);
            client->CurrentState = SD_CLIENT_SERVICE_DOWN;
            client->FindDelayTimerOn = TRUE;
        }
        else
        {
            /** @req 4.2.2/SWS_SD_00352 */
            if (entry1 != (Sd_Entry_Type1_Services *) NULL) {
                if ((entry1->Type == OFFER_SERVICE_TYPE) && (entry1->TTL > 0)) {
                    /** @req 4.2.2/SWS_SD_00604 */
                    OpenSocketConnections(client);

                    /** @req 4.2.2/SWS_SD_00456 */
                    client->FindDelay_Timer_Value_ms = 0;
                    client->FindDelayTimerOn = FALSE;
                    client->CurrentState = SD_CLIENT_SERVICE_AVAILABLE;
                    client->Phase = SD_MAIN_PHASE; /* INITIAL_WAIT -> MAIN */
                    if (entry1->TTL < TTL_TIMER_MAX) {
                        client->TTL_Timer_Value_ms = entry1->TTL * 1000u;
                    }
                    client->TTL_Timer_Running = TRUE;
                    /* OpenTCP connection if SdClientServiceTcpRef is configured and was not opened before */
                    OpenTcpConnection(client);

                    SetMethodsRemoteAddress(client);

                    /* Send out SubscribeEventGroup entries for all REQUESTED eventgroups */
                    for (uint8 event_group_index= 0; event_group_index < client->ClientServiceCfg->NoOfConsumedEventGroups; event_group_index++){
                        if (client->ConsumedEventGroups[event_group_index].ConsumedEventGroupMode == SD_CONSUMED_EVENTGROUP_REQUESTED) {
                            TransmitSdMessage(sd_instance, client, NULL, NULL, event_group_index, SD_SUBSCRIBE_EVENTGROUP, &ipaddress, is_multicast);
                            client->ConsumedEventGroups[event_group_index].Acknowledged = FALSE;
                        }
                    }

                    break;
                }
            }

            /** @req 4.2.2/SWS_SD_00353 */
            /* IMPROVEMENT: Check the calculation for the timer step. */
            client->FindDelay_Timer_Value_ms -= SD_MAIN_FUNCTION_CYCLE_TIME_MS;
            if (client->FindDelay_Timer_Value_ms <= 0) {
                /* Send FindService Entry */
                TransmitSdMessage(sd_instance, client, NULL, NULL, 0, SD_FIND_SERVICE, &ipaddress,FALSE);
                /** @req 4.2.2/SWS_SD_00456 */
                client->FindDelayTimerOn = FALSE;
                if (client->ClientServiceCfg->TimerRef->InitialFindRepetitionsMax == 0){
                    client->Phase = SD_MAIN_PHASE; /* INITIAL_WAIT -> MAIN */
                    client->TTL_Timer_Value_ms = client->ClientServiceCfg->TimerRef->TTL * 1000u;  /* TBD: No req so not sure if it should be done here */
                    client->TTL_Timer_Running = TRUE;
                }
                else {
                    client->Phase = SD_REPETITION_PHASE; /* INITIAL_WAIT -> REPETITION */
                    client->RepetitionFactor = 1;
                    client->FindRepetitions = 0;
                    client->TTL_Timer_Running = FALSE;
                }
                break;
            }
        }

        break;
    case SD_REPETITION_PHASE:
        /** @req 4.2.2/SWS_SD_00371 */
        if (client->ClientServiceMode == SD_CLIENT_SERVICE_RELEASED) {
            client->FindRepDelay_Timer_Value_ms = 0;
            client->FindRepDelayTimerOn = FALSE;
            client->Phase = SD_DOWN_PHASE; /* REPETITION -> DOWN */
            client->CurrentState = SD_CLIENT_SERVICE_DOWN;

            /** @req 4.2.2/SWS_SD_00374 */
            /** @req 4.2.2/SWS_SD_00024 */
            /* Call SoAd_DisableSpecificRouting() for all Socket Connections
             * referenced by this client service instance. IMPROVEMENT: IS this correct?*/
            (void)SoAd_DisableSpecificRouting
                (client->ClientServiceCfg->ConsumedMethods.ClientServiceActivationRef,
                        client->ClientServiceCfg->UdpSocketConnectionGroupId);

            /** @req 4.2.2/SWS_SD_00603 */
            if (client->SocketConnectionOpened){
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this client service instance */
                CloseSocketConnections(client, TRUE);  // IMPROVEMENT: Investigate abort parameter
            }
            break;
        }

        /** @req 4.2.2/SWS_SD_00373 */
        if ((!sd_instance->TxPduIpAddressAssigned)) {
            client->FindRepDelay_Timer_Value_ms = 0;
            client->FindRepDelayTimerOn = FALSE;
            client->Phase = SD_DOWN_PHASE; /* REPETITION -> DOWN */

            /* Set all ConsumedEventGroupCurrentState to DOWN */
            for (uint32 eg = 0; eg < client->ClientServiceCfg->NoOfConsumedEventGroups; eg++){
                BswM_Sd_ConsumedEventGroupCurrentState(
                        client->ClientServiceCfg->ConsumedEventGroup[eg].HandleId,
                        SD_CONSUMED_EVENTGROUP_DOWN);
            }

            client->CurrentState = SD_CLIENT_SERVICE_DOWN;

            /** @req 4.2.2/SWS_SD_00603 */
            if (client->SocketConnectionOpened){
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this client service instance */
                CloseSocketConnections(client, TRUE);  // IMPROVEMENT: Investigate abort parameter
            }
            break;

        }

        if (!client->FindRepDelayTimerOn) {
            /** @req 4.2.2/SWS_SD_00358 */
            /* Start FindRepDelay Timer */
            client->FindRepDelay_Timer_Value_ms = client->RepetitionFactor * client->ClientServiceCfg->TimerRef->InitialFindRepetitionBaseDelay_ms;
            client->FindRepDelayTimerOn = TRUE;
        }
//		else
//		{
            /** @req 4.2.2/SWS_SD_00365 */
            if (entry1 != (Sd_Entry_Type1_Services *) NULL){
                if ((entry1->Type == OFFER_SERVICE_TYPE)) {
                    client->FindRepDelay_Timer_Value_ms = 0;
                    client->FindRepDelayTimerOn = FALSE;
                    client->CurrentState = SD_CLIENT_SERVICE_AVAILABLE;
                    client->Phase = SD_MAIN_PHASE; /* REPETITION -> MAIN */
                    if (entry1->TTL < TTL_TIMER_MAX) {
                        client->TTL_Timer_Value_ms = entry1->TTL * 1000u;
                    }
                    client->TTL_Timer_Running = TRUE;
                    /* OpenTCP connection if SdClientServiceTcpRef is configured and was not opened before */
                    OpenTcpConnection(client);

                    SetMethodsRemoteAddress(client);

                    /* Send out SubscribeEventGroup entries for all REQUESTED eventgroups */
                    for (uint8 event_group_index=0; event_group_index < client->ClientServiceCfg->NoOfConsumedEventGroups; event_group_index++){
                        if (client->ConsumedEventGroups[event_group_index].ConsumedEventGroupMode == SD_CONSUMED_EVENTGROUP_REQUESTED) {
                            TransmitSdMessage(sd_instance, client, NULL, NULL, event_group_index, SD_SUBSCRIBE_EVENTGROUP, &ipaddress, is_multicast);
                            client->ConsumedEventGroups[event_group_index].Acknowledged = FALSE;
                        }
                    }

                    break;
                }
            }

            /** @req 4.2.2/SWS_SD_00363 */
            client->FindRepDelay_Timer_Value_ms -= SD_MAIN_FUNCTION_CYCLE_TIME_MS;

            /** @req 4.2.2/SWS_SD_00457 */
            if (client->FindRepDelay_Timer_Value_ms <= 0) {
                /* Send FindService Entry */
                TransmitSdMessage(sd_instance, client, NULL, NULL, 0, SD_FIND_SERVICE, &ipaddress, FALSE);
                client->FindRepDelayTimerOn = FALSE;
                client->FindRepetitions++;
                client->RepetitionFactor = client->RepetitionFactor * 2; /* Doubles the interval for next FindService */

                /** @req 4.2.2/SWS_SD_00369 */
                if (client->FindRepetitions >= client->ClientServiceCfg->TimerRef->InitialFindRepetitionsMax){
                    client->Phase = SD_MAIN_PHASE; /* REPETITION -> MAIN */
                    client->TTL_Timer_Value_ms = client->ClientServiceCfg->TimerRef->TTL * 1000u;  /* TBD: No req so not sure if it should be done here */
                    client->TTL_Timer_Running = TRUE;
                    break;
                }

            }
//		}


        break;
    case SD_MAIN_PHASE:
        /** @req 4.2.2/SWS_SD_00600 */
        if (client->TTL_Timer_Running){
            client->TTL_Timer_Value_ms -= SD_MAIN_FUNCTION_CYCLE_TIME_MS;
        }
        /* Check TTL timeout */
        if (client->TTL_Timer_Value_ms == 0u){
            client->Phase = SD_INITIAL_WAIT_PHASE; /* MAIN -> INITIAL_WAIT */
            client->CurrentState = SD_CLIENT_SERVICE_DOWN;

            for (uint32 eg = 0; eg < client->ClientServiceCfg->NoOfConsumedEventGroups; eg++){
                if (client->ConsumedEventGroups[eg].ConsumedEventGroupMode == SD_CONSUMED_EVENTGROUP_REQUESTED) {
                    client->ConsumedEventGroups[eg].TTL_Timer_Value_ms = 0u;
                    client->ConsumedEventGroups[eg].ConsumedEventGroupState = SD_CONSUMED_EVENTGROUP_DOWN;
                    BswM_Sd_ConsumedEventGroupCurrentState(
                            client->ClientServiceCfg->ConsumedEventGroup[eg].HandleId,
                            client->ConsumedEventGroups[eg].ConsumedEventGroupState);

                }
            }

            break;
        }

        /** @req 4.2.2/SWS_SD_00601 */
        for (uint32 eg = 0; eg < client->ClientServiceCfg->NoOfConsumedEventGroups; eg++){
            if (client->ConsumedEventGroups[eg].ConsumedEventGroupMode == SD_CONSUMED_EVENTGROUP_REQUESTED) {
                client->ConsumedEventGroups[eg].TTL_Timer_Value_ms -= SD_MAIN_FUNCTION_CYCLE_TIME_MS;
                if (client->ConsumedEventGroups[eg].TTL_Timer_Value_ms <= 0u) {
                    client->ConsumedEventGroups[eg].TTL_Timer_Value_ms = 0u;
                    client->ConsumedEventGroups[eg].ConsumedEventGroupState = SD_CONSUMED_EVENTGROUP_DOWN;
                    BswM_Sd_ConsumedEventGroupCurrentState(
                            client->ClientServiceCfg->ConsumedEventGroup[eg].HandleId,
                            client->ConsumedEventGroups[eg].ConsumedEventGroupState);
                }
            }
        }


        /** @req 4.2.2/SWS_SD_00376 */
        if (entry1 != (Sd_Entry_Type1_Services *) NULL){
            if ((entry1->Type == OFFER_SERVICE_TYPE) && entry1->TTL != 0) {
                client->CurrentState = SD_CLIENT_SERVICE_AVAILABLE;

                /* OpenTCP connection if SdClientServiceTcpRef is configured and was not opened before */
                OpenTcpConnection(client);

                SetMethodsRemoteAddress(client);

                if (entry1->TTL < TTL_TIMER_MAX) {
                    client->TTL_Timer_Value_ms = entry1->TTL * 1000u;
                }
                client->TTL_Timer_Running = TRUE;

                for (uint32 eg = 0; eg < client->ClientServiceCfg->NoOfConsumedEventGroups; eg++){
                    if (client->ConsumedEventGroups[eg].ConsumedEventGroupMode == SD_CONSUMED_EVENTGROUP_REQUESTED) {

                        /* StopSubscribeEventgroup entry shall be sent out,
                         * if the last SubscribeEventgroup entry was sent as reaction to an OfferService entry
                         * received via Multicast, it was never answered with a SubscribeEventgroupAck,
                         * and the current OfferService entry was received via Multicast. */
						if ((client->ConsumedEventGroups[eg].ConsumedEventGroupState == SD_CONSUMED_EVENTGROUP_AVAILABLE) &&
                           (!client->ConsumedEventGroups[eg].Acknowledged)) {
                            TransmitSdMessage(sd_instance, client, NULL, NULL, eg, SD_STOP_SUBSCRIBE_EVENTGROUP, &ipaddress, is_multicast);
							client->ConsumedEventGroups[eg].ConsumedEventGroupState = SD_CONSUMED_EVENTGROUP_DOWN;
						}

						if (client->ConsumedEventGroups[eg].ConsumedEventGroupState == SD_CONSUMED_EVENTGROUP_DOWN){
							/* Send out SubscribeEventGroup entries */
							TransmitSdMessage(sd_instance, client, NULL, NULL, eg, SD_SUBSCRIBE_EVENTGROUP, &ipaddress, is_multicast);
							client->ConsumedEventGroups[eg].Acknowledged = FALSE;
						}
                    }
                }

                break;
            }
            /** @req 4.2.2/SWS_SD_00367 */
            /** @req 4.2.2/SWS_SD_00422 */
            else if ((entry1->Type == STOP_OFFER_SERVICE_TYPE) && (entry1->TTL == 0)) {

                /* Stop the TTL timers */
                client->TTL_Timer_Running = FALSE;

                for (uint32 eg = 0; eg < client->ClientServiceCfg->NoOfConsumedEventGroups; eg++){
                    if (client->ConsumedEventGroups[eg].ConsumedEventGroupMode == SD_CONSUMED_EVENTGROUP_REQUESTED) {
                        client->ConsumedEventGroups[eg].TTL_Timer_Value_ms = 0u;
                    }
                }

                /* Report the client service as DOWN */
                if (client->CurrentState == SD_CLIENT_SERVICE_AVAILABLE) {
                    client->CurrentState = SD_CLIENT_SERVICE_DOWN;
                }

                /* Report all ConsumedEventGroups as DOWN */
                for (uint32 eg = 0; eg < client->ClientServiceCfg->NoOfConsumedEventGroups; eg++){
                    if (client->ConsumedEventGroups[eg].ConsumedEventGroupState == SD_CONSUMED_EVENTGROUP_AVAILABLE) {
                        client->ConsumedEventGroups[eg].ConsumedEventGroupState = SD_CONSUMED_EVENTGROUP_DOWN;
                        BswM_Sd_ConsumedEventGroupCurrentState(
                                client->ClientServiceCfg->ConsumedEventGroup[eg].HandleId,
                                SD_CONSUMED_EVENTGROUP_DOWN);
                    }
                }

            }

        }else if (entry2 != ((Sd_Entry_Type2_EventGroups *) NULL)){
            /** @req 4.2.2/SWS_SD_00377 */
            if ((entry2->Type == SUBSCRIBE_EVENTGROUP_ACK_TYPE)) {

                for (uint8 eh=0; eh < client->ClientServiceCfg->NoOfConsumedEventGroups; eh++) {
                    if (client->ClientServiceCfg->ConsumedEventGroup[eh].Id == entry2->EventgroupID) {

                        /* Use the information of the Multicast Option (if existing) to set up relevant Multicast Information in SoAd */
                        if (client->ConsumedEventGroups[eh].MulticastAddress.valid == TRUE){
                            for (uint8 grp=0; grp < client->ClientServiceCfg->ConsumedEventGroup[eh].NoOfMulticastGroups; grp++){
                                if (client->ClientServiceCfg->ConsumedEventGroup[eh].MulticastGroupRef[grp] != SOCKET_CONNECTION_GROUP_NOT_SET) {
                                    TcpIp_SockAddrType LocalAddr;
                                    uint8 netmask;
                                    TcpIp_SockAddrType DefaultRouter;
                                    LocalAddr.domain = TCPIP_AF_INET;
                                    DefaultRouter.domain = TCPIP_AF_INET;
                                    (void)SoAd_GetLocalAddr(client->ClientServiceCfg->ConsumedEventGroup[eh].MulticastGroupRef[grp],
                                            &LocalAddr, &netmask, &DefaultRouter);

                                    LocalAddr.addr[3] = ((client->ConsumedEventGroups[eh].MulticastAddress.IPv4Address & 0xFF000000) >> 24);
                                    LocalAddr.addr[2] = ((client->ConsumedEventGroups[eh].MulticastAddress.IPv4Address & 0x00FF0000) >> 16);
                                    LocalAddr.addr[1] = ((client->ConsumedEventGroups[eh].MulticastAddress.IPv4Address & 0x0000FF00) >> 8);
                                    LocalAddr.addr[0] = (client->ConsumedEventGroups[eh].MulticastAddress.IPv4Address & 0x000000FF);
                                    LocalAddr.domain = TCPIP_AF_INET;
                                    LocalAddr.port = client->ConsumedEventGroups[eh].MulticastAddress.PortNumber;

                                    (void)SoAd_RequestIpAddrAssignment(
                                            client->ClientServiceCfg->ConsumedEventGroup[eh].MulticastGroupRef[grp],
                                            TCPIP_IPADDR_ASSIGNMENT_STATIC, &LocalAddr,
                                            netmask, &DefaultRouter);

                                    /** @req 4.2.2/SWS_SD_00704 */
                                    /*  Call SoAd_EnableSpecificRouting() */
                                    (void)SoAd_EnableSpecificRouting(
                                            client->ClientServiceCfg->ConsumedEventGroup[eh].MulticastActivationRef,
                                            client->ClientServiceCfg->ConsumedEventGroup[eh].MulticastGroupRef[grp]);

                                }
                            }

                        }

                        client->ConsumedEventGroups[eh].Acknowledged = TRUE;

                        /* Call BswM_Sd_ConsumedEventGroupCurrentState with SD_CONSUMED_EVENTGROUP_AVAILABLE. */
                        client->ConsumedEventGroups[eh].ConsumedEventGroupState = SD_CONSUMED_EVENTGROUP_AVAILABLE;
                        BswM_Sd_ConsumedEventGroupCurrentState(client->ClientServiceCfg->ConsumedEventGroup[eh].HandleId, SD_CONSUMED_EVENTGROUP_AVAILABLE);

                        /* Setup TTL timer with the TTL of the SubscribeEventgroupAck entry. */
                        client->ConsumedEventGroups[eh].TTL_Timer_Value_ms = entry2->TTL * 1000u;

                        break;
                    }
                }


            }
            /** @req 4.2.2/SWS_SD_00465 */
            else if ((entry2->Type == SUBSCRIBE_EVENTGROUP_NACK_TYPE)) {

                /* IMPROVEMENT:  If condition fulfilled according to requirement 4.2.2/SWS_SD_00465:
                 * Report the DEM error SD_E_SUBSCR_NACK_RECV and restart the TCP connection (if applicable) */

                /* Call SoAd_CloseSoCon(abort=TRUE) to close all socket connections for this service instance
                 * Call SoAd_OpenSoCon() to reopen all socket connections for this service instance
                 */
                 CloseSocketConnections(client, TRUE);
                 OpenSocketConnections(client);
            }
        }

        /** @req 4.2.2/SWS_SD_00381 */
        /** @req 4.2.2/SWS_SD_00380 */
        /** @req 4.2.2/SWS_SD_00375 */
        if ((client->ClientServiceMode == SD_CLIENT_SERVICE_RELEASED) ||
            (!sd_instance->TxPduIpAddressAssigned)) {
            client->Phase = SD_DOWN_PHASE; /* MAIN -> DOWN */
            client->CurrentState = SD_CLIENT_SERVICE_DOWN;
            client->TTL_Timer_Value_ms = 0u;
            client->OfferActive = FALSE;

            /* Report all ConsumedEventGroups as DOWN */
            for (uint32 eg = 0; eg < client->ClientServiceCfg->NoOfConsumedEventGroups; eg++) {
                if (client->ConsumedEventGroups[eg].ConsumedEventGroupState == SD_CONSUMED_EVENTGROUP_AVAILABLE) {

                    /* Send out StopSubscribeEventgroup entry, */
                    TransmitSdMessage(sd_instance, client, NULL, NULL, eg, SD_STOP_SUBSCRIBE_EVENTGROUP, &ipaddress, FALSE);

                    /** @req 4.2.2/SWS_SD_00711 */
                    /* Call SoAd_DisableSpecificRouting() for all socket connections associated with this client service */
                    if (client->ClientServiceCfg->ConsumedEventGroup[eg].TcpActivationRef != ACTIVATION_REF_NOT_SET) {
                        (void)SoAd_DisableSpecificRouting
                                (client->ClientServiceCfg->ConsumedEventGroup[eg].TcpActivationRef,
                                 client->ClientServiceCfg->TcpSocketConnectionGroupId);
                    }
                    else if (client->ClientServiceCfg->ConsumedEventGroup[eg].UdpActivationRef != ACTIVATION_REF_NOT_SET) {
                        (void)SoAd_DisableSpecificRouting
                                (client->ClientServiceCfg->ConsumedEventGroup[eg].UdpActivationRef,
                                 client->ClientServiceCfg->UdpSocketConnectionGroupId);
                    }
                    if (client->ConsumedEventGroups[eg].MulticastAddress.valid == TRUE){
                        for (uint8 grp=0; grp < client->ClientServiceCfg->ConsumedEventGroup[eg].NoOfMulticastGroups; grp++){
                            (void)SoAd_DisableSpecificRouting
                                    (client->ClientServiceCfg->ConsumedEventGroup[eg].MulticastActivationRef,
                                     client->ClientServiceCfg->ConsumedEventGroup[eg].MulticastGroupRef[grp]);
                        }
                    }

                    client->ConsumedEventGroups[eg].ConsumedEventGroupState = SD_CONSUMED_EVENTGROUP_DOWN;
                    /* NOTE! Req. 4.2.2/SWS_SD_00381 says REQUESTED but not compatible type using SD_CONSUMED_EVENTGROUP_DOWN instead */
                    BswM_Sd_ConsumedEventGroupCurrentState(
                            client->ClientServiceCfg->ConsumedEventGroup[eg].HandleId,
                            SD_CONSUMED_EVENTGROUP_DOWN);
                }
            }
            /** @req 4.2.2/SWS_SD_00382 */
            /** @req 4.2.2/SWS_SD_00024 */
            /* Call SoAd_DisableSpecificRouting() for all socket connections associated with this client service */
            (void)SoAd_DisableSpecificRouting
                (client->ClientServiceCfg->ConsumedMethods.ClientServiceActivationRef,
                        client->ClientServiceCfg->UdpSocketConnectionGroupId);

            /* Clear set endpoint options for this client */
            client->TcpEndpoint.valid = FALSE;
            client->UdpEndpoint.valid = FALSE;
        }

        break;
    }

    /* Indicate CurrentState to BswM */
    BswM_Sd_ClientServiceCurrentState(client->ClientServiceCfg->HandleId,client->CurrentState);

    /* Clear flag that indicates that SdInit was called since last Sd_MainFunction cycle */
    sd_instance->SdInitCalled = FALSE;
}





