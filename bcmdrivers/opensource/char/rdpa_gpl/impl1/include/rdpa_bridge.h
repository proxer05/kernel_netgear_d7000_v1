/*
* <:copyright-BRCM:2013:DUAL/GPL:standard
* 
*    Copyright (c) 2013 Broadcom Corporation
*    All Rights Reserved
* 
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, version 2, as published by
* the Free Software Foundation (the "GPL").
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* 
* A copy of the GPL is available at http://www.broadcom.com/licenses/GPLv2.php, or by
* writing to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
* 
* :> 
*/


#ifndef _RDPA_BRIDGE_H_
#define _RDPA_BRIDGE_H_

/** \defgroup bridge Bridge
 * RDPA supports multiple 802.1d/q bridges.\n
 * L2 bridge functionality:
 * - All bridge objects share the same MAC table (named FDB=forwarding DataBase)
 *	 - Use ::rdpa_bridge_mac_set to add/delete MAC entry to/from the FDB
 * - Each bridge object is compounded of interfaces of the folowing type:
 *   - Port object  bridge interface without VLAN configuration
 *   - VLAN object - bridge interface with VLAN configuration
 *   - Use ::bdmf_link to associate an interface (Port/VLAN) to the bridge
 * - The port forwording matrix is updated each time a new interface is added to the bridge object:
 * - The new interface (Port/VLAN) can forword packets to all interfaces under the same bridge
 * 
 * @{
 */

/** Bridge type */
typedef enum {
    rdpa_bridge_802_1d, /**< Subset IEEE MAC Bridges standard, VLAN is not supported */
    rdpa_bridge_802_1q /**< Subset IEEE Networking standard that supports Virtual LANs (VLANs) */
} rdpa_bridge_type;

/** Bridge configuration */
typedef struct {
    rdpa_bridge_type type; /**< Bridge type. Set automatically based on the system configuration */
    bdmf_boolean auto_forward; /**< Update forwarding matrix automatically when ports/VLANs are connected or
                                 disconnected */
    bdmf_boolean auto_aggregate; /**< Update aggregation table automatically when ports/VLANs are connected or
                                   disconnected */
} rdpa_bridge_cfg_t;


#define RDPA_BRIDGE_MAX_BRIDGES 8 /**< Max number of bridge objects */
#define RDPA_BRIDGE_MAX_VIDS 128 /**< Max number of VIDs per bridge */
#define RDPA_BRIDGE_MAX_FDB_ENTRIES 1024 /**< Max number of entries in FDB, shared for all bridge objects */

/** FDB key
 * Forwarding DataBase entry key 
 */
typedef struct {
    uint16_t vid; /**< Input parameter: VLAN ID or ignored in case of bridge type ::rdpa_bridge_802_1d*/
    bdmf_mac_t mac; /**< Input parameter: MAC address */
    uint16_t entry; /**< Output parameter: Entry handle that is recieved after adding new MAC to the FDB. Can be used
                      for optimizations */
} rdpa_fdb_key_t;

/** FDB data
 * Forwarding DataBase entry configuration
 */
typedef struct {
    rdpa_ports ports; /**< Bit-Mask ports to forward the MAC to */
    rdpa_forward_action sa_action; /**< Action if MAC is found by SA lookup */
    rdpa_forward_action da_action; /**< Action if MAC is found by DA lookup */
} rdpa_fdb_data_t;

/** @} end of bridge Doxygen group */

#endif /* _RDPA_BRIDGE_H_ */
