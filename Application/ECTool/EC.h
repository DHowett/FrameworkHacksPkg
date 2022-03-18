#pragma once

#include <Library/CrosECLib.h>

#define G_EC_MAX_REQUEST  (EC_LPC_HOST_PACKET_SIZE - sizeof(struct ec_host_request))
#define G_EC_MAX_RESPONSE (EC_LPC_HOST_PACKET_SIZE - sizeof(struct ec_host_response))

void PrintECResponse(int rv);
