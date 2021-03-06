//******************************************************************
//
// Copyright 2015 Intel Mobile Communications GmbH All Rights Reserved.
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include "../../../include/ocstack.h"
#include "../../../../logger/include/logger.h"
#include "../../../include/ocpayload.h"
#include "../../../include/payload_logging.h"
#include "ocremoteaccessclient.h"

#define SET_BUT_NOT_USED(x) (void) x
// Tracking user input
static int TEST_CASE = 0;

static const char *MULTICAST_RESOURCE_DISCOVERY_QUERY = "/oic/res";

static std::string coapServerIP = "255.255.255.255";
static std::string coapServerPort = "5683";
static std::string coapServerResource = "/a/light";
static OCDevAddr responseAddr;
//Use ipv4addr for both InitDiscovery and InitPlatformOrDeviceDiscovery
char remoteServerJabberID[MAX_ADDR_STR_SIZE];
void StripNewLineChar(char *str);
static uint16_t maxNotification = 15;

// The handle for the observe registration
OCDoHandle gObserveDoHandle;
#ifdef WITH_PRESENCE
// The handle for observe registration
OCDoHandle gPresenceHandle;
#endif
// After this crosses a threshold client deregisters for further notifications
int gNumObserveNotifies = 0;

#ifdef WITH_PRESENCE
int gNumPresenceNotifies = 0;
#endif

int gQuitFlag = 0;
/* SIGINT handler: set gQuitFlag to 1 for graceful termination */
void handleSigInt(int signum)
{
  if (signum == SIGINT)
  {
    gQuitFlag = 1;
  }
}

OCPayload *putPayload()
{
  OCRepPayload *payload = OCRepPayloadCreate();

  if (!payload)
  {
    std::cout << "Failed to create put payload object" << std::endl;
    std::exit(1);
  }
  OCRepPayloadSetPropInt(payload, "power", 42);
  OCRepPayloadSetPropBool(payload, "state", true);
  return (OCPayload *) payload;
}

static void PrintUsage()
{
  OIC_LOG(INFO, TAG, "This sample makes all requests via the remote access adapter");
  OIC_LOG(INFO, TAG, "Usage : ocremoteaccessclient -t <number>");
  OIC_LOG(INFO, TAG, "-t 1  :  Discover Resources");
  OIC_LOG(INFO, TAG, "-t 2  :  Discover & Get");
  OIC_LOG(INFO, TAG, "-t 3  :  Discover & Put");
  OIC_LOG(INFO, TAG, "-t 4  :  Discover & Post");
  OIC_LOG(INFO, TAG, "-t 5  :  Discover & Delete");
  OIC_LOG(INFO, TAG, "-t 6  :  Discover & Observe");
  OIC_LOG(INFO, TAG, "-t 7  :  Discover & Observe then cancel immediately with High QOS");
}

OCStackResult InvokeOCDoResource(std::ostringstream &query,
                                 OCMethod method,
                                 OCQualityOfService qos,
                                 OCClientResponseHandler cb,
                                 OCHeaderOption *options,
                                 uint8_t numOptions)
{
  OCCallbackData cbData;
  OCDoHandle handle;

  cbData.cb       = cb;
  cbData.context  = (void *)DEFAULT_CONTEXT_VALUE;
  cbData.cd       = NULL;

  OCStackResult ret = OCDoResource(
                        &handle,
                        method,
                        query.str().c_str(),
                        &responseAddr,
                        (method == OC_REST_PUT) ? putPayload() : NULL,
                        CT_ADAPTER_REMOTE_ACCESS,
                        qos,
                        &cbData,
                        options,
                        numOptions);

  if (ret != OC_STACK_OK)
  {
    OIC_LOG_V(ERROR, TAG, "OCDoResource returns error %d with method %d", ret, method);
  }
  else if (method == OC_REST_OBSERVE || method == OC_REST_OBSERVE_ALL)
  {
    gObserveDoHandle = handle;
  }
#ifdef WITH_PRESENCE
  else if (method == OC_REST_PRESENCE)
  {
    gPresenceHandle = handle;
  }
#endif

  return ret;
}

OCStackApplicationResult restRequestCB(void *ctx,
                                       OCDoHandle handle, OCClientResponse *clientResponse)
{
  if (clientResponse == NULL)
  {
    OIC_LOG(INFO, TAG, "Received NULL response");
    return   OC_STACK_DELETE_TRANSACTION;
  }
  if (ctx == (void *)DEFAULT_CONTEXT_VALUE)
  {
    OIC_LOG(INFO, TAG, "Callback Context recvd successfully");
  }

  OIC_LOG_V(INFO, TAG, "StackResult: %s",  getResult(clientResponse->result));
  OIC_LOG_V(INFO, TAG, "SEQUENCE NUMBER: %d", clientResponse->sequenceNumber);
  OIC_LOG_PAYLOAD(INFO, clientResponse->payload);

  if (clientResponse->numRcvdVendorSpecificHeaderOptions > 0)
  {
    OIC_LOG(INFO, TAG, "Received vendor specific options. Ignoring");
  }
  SET_BUT_NOT_USED(handle);
  return OC_STACK_DELETE_TRANSACTION;
}

OCStackApplicationResult obsReqCB(void *ctx, OCDoHandle handle, OCClientResponse *clientResponse)
{
  if (!clientResponse)
  {
    OIC_LOG_V(INFO, TAG, "obsReqCB received NULL response");
  }
  if (ctx == (void *)DEFAULT_CONTEXT_VALUE)
  {
    OIC_LOG(INFO, TAG, "Callback Context recvd successfully");
  }
  OIC_LOG_V(INFO, TAG, "StackResult: %s",  getResult(clientResponse->result));
  OIC_LOG_V(INFO, TAG, "SEQUENCE NUMBER: %d", clientResponse->sequenceNumber);
  OIC_LOG_V(INFO, TAG, "OBSERVE notification %d recvd", gNumObserveNotifies);
  OIC_LOG_PAYLOAD(INFO, clientResponse->payload);

  gNumObserveNotifies++;
  if (gNumObserveNotifies == maxNotification)
  {
    if (OCCancel(gObserveDoHandle, OC_LOW_QOS, NULL, 0) != OC_STACK_OK)
    {
      OIC_LOG(ERROR, TAG, "Observe cancel error");
    }
    return OC_STACK_DELETE_TRANSACTION;
  }
  if (gNumObserveNotifies == 1 && TEST_CASE == TEST_OBS_REQ_NON_CANCEL_IMM)
  {
    if (OCCancel(gObserveDoHandle, OC_HIGH_QOS, NULL, 0) != OC_STACK_OK)
    {
      OIC_LOG(ERROR, TAG, "Observe cancel error");
    }
  }
  if (clientResponse->sequenceNumber == OC_OBSERVE_REGISTER)
  {
    OIC_LOG(INFO, TAG, "Registration confirmed");
  }
  else if (clientResponse->sequenceNumber == OC_OBSERVE_DEREGISTER)
  {
    OIC_LOG(INFO, TAG, "de-registration confirmed");
    return OC_STACK_DELETE_TRANSACTION;
  }
  else if (clientResponse->sequenceNumber == OC_OBSERVE_NO_OPTION)
  {
    OIC_LOG(INFO, TAG, "Registration/deregistration failed");
    return OC_STACK_DELETE_TRANSACTION;
  }

  SET_BUT_NOT_USED(handle);
  return OC_STACK_KEEP_TRANSACTION;
}
#ifdef WITH_PRESENCE
OCStackApplicationResult presenceCB(void *ctx,
                                    OCDoHandle handle, OCClientResponse *clientResponse)
{
  if (ctx == (void *)DEFAULT_CONTEXT_VALUE)
  {
    OIC_LOG(INFO, TAG, "Callback Context recvd successfully");
  }

  if (clientResponse)
  {
    OIC_LOG_V(INFO, TAG, "StackResult: %s", getResult(clientResponse->result));
    OIC_LOG_V(INFO, TAG, "NONCE NUMBER: %u", clientResponse->sequenceNumber);
    OIC_LOG_V(INFO, TAG, "PRESENCE notification %d recvd", gNumPresenceNotifies);
    OIC_LOG_PAYLOAD(INFO, clientResponse->payload);

    gNumPresenceNotifies++;
    if (gNumPresenceNotifies == maxNotification)
    {
      if (OCCancel(gPresenceHandle, OC_LOW_QOS, NULL, 0) != OC_STACK_OK)
      {
        OIC_LOG(ERROR, TAG, "Presence cancel error");
      }
      return OC_STACK_DELETE_TRANSACTION;
    }
  }
  else
  {
    OIC_LOG_V(INFO, TAG, "presenceCB received Null clientResponse");
  }
  SET_BUT_NOT_USED(handle);
  return OC_STACK_KEEP_TRANSACTION;
}
#endif

// This is a function called back when a device is discovered
OCStackApplicationResult discoveryReqCB(void *ctx, OCDoHandle handle,
                                        OCClientResponse *clientResponse)
{
  if (ctx == (void *) DEFAULT_CONTEXT_VALUE)
  {
    OIC_LOG(INFO, TAG, "DISCOVER  callback recvd");
  }

  if (!clientResponse)
  {
    OIC_LOG_V(INFO, TAG, "discoveryReqCB received Null clientResponse");
  }

  OIC_LOG_V(INFO, TAG, "StackResult: %s", getResult(clientResponse->result));
  OIC_LOG_PAYLOAD(INFO, clientResponse->payload);

  responseAddr = clientResponse->devAddr;

  switch (TEST_CASE)
  {
      OIC_LOG_V(INFO, TAG, "TEST_CASE %u\n", TEST_CASE);
    case TEST_GET_REQ_NON:
      InitGetRequest(OC_LOW_QOS);
      break;
    case TEST_PUT_REQ_NON:
      InitPutRequest(OC_LOW_QOS);
      break;
    case TEST_POST_REQ_NON:
      InitPostRequest(OC_LOW_QOS);
      break;
    case TEST_DELETE_REQ_NON:
      InitDeleteRequest(OC_LOW_QOS);
      break;
    case TEST_OBS_REQ_NON:
    case TEST_OBS_REQ_NON_CANCEL_IMM:
      InitObserveRequest(OC_LOW_QOS);
      break;
    default:
      PrintUsage();
      break;
  }
  SET_BUT_NOT_USED(handle);
  return OC_STACK_KEEP_TRANSACTION;
}

OCStackApplicationResult PlatformDiscoveryReqCB(void *ctx, OCDoHandle handle,
                                                OCClientResponse *clientResponse)
{
  if (ctx == (void *) DEFAULT_CONTEXT_VALUE)
  {
    OIC_LOG(INFO, TAG, "Callback Context for Platform DISCOVER query recvd successfully");
  }

  if (clientResponse)
  {
    OIC_LOG_PAYLOAD(INFO, clientResponse->payload);
  }
  else
  {
    OIC_LOG_V(INFO, TAG, "PlatformDiscoveryReqCB received Null clientResponse");
  }

  SET_BUT_NOT_USED(handle);
  return OC_STACK_DELETE_TRANSACTION;
}

OCStackApplicationResult DeviceDiscoveryReqCB(void *ctx, OCDoHandle handle,
                                              OCClientResponse *clientResponse)
{
  if (ctx == (void *) DEFAULT_CONTEXT_VALUE)
  {
    OIC_LOG(INFO, TAG, "Callback Context for Device DISCOVER query recvd successfully");
  }

  if (clientResponse)
  {
    OIC_LOG_PAYLOAD(INFO, clientResponse->payload);
  }
  else
  {
    OIC_LOG_V(INFO, TAG, "PlatformDiscoveryReqCB received Null clientResponse");
  }

  SET_BUT_NOT_USED(handle);
  return OC_STACK_DELETE_TRANSACTION;
}

int InitObserveRequest(OCQualityOfService qos)
{
  OIC_LOG_V(INFO, TAG, "\n\nExecuting %s", __func__);
  std::ostringstream query;
  query << coapServerResource;
  return (InvokeOCDoResource(query,
                             OC_REST_OBSERVE, (qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS, obsReqCB, NULL, 0));
}

int InitPutRequest(OCQualityOfService qos)
{
  OIC_LOG_V(INFO, TAG, "\n\nExecuting %s", __func__);
  std::ostringstream query;
  query << coapServerResource;
  return (InvokeOCDoResource(query, OC_REST_PUT, (qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS,
                             restRequestCB, NULL, 0));
}

int InitPostRequest(OCQualityOfService qos)
{
  OIC_LOG_V(INFO, TAG, "\n\nExecuting %s", __func__);
  std::ostringstream query;
  query << coapServerResource;
  // First POST operation (to create an Light instance)
  OCStackResult result = InvokeOCDoResource(query, OC_REST_POST,
                                            ((qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS),
                                            restRequestCB, NULL, 0);
  if (OC_STACK_OK != result)
  {
    // Error can happen if for example, network connectivity is down
    OIC_LOG(INFO, TAG, "First POST call did not succeed");
  }

  // Second POST operation (to create an Light instance)
  result = InvokeOCDoResource(query, OC_REST_POST,
                              ((qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS),
                              restRequestCB, NULL, 0);
  if (OC_STACK_OK != result)
  {
    OIC_LOG(INFO, TAG, "Second POST call did not succeed");
  }

  // This POST operation will update the original resourced /a/light
  return (InvokeOCDoResource(query, OC_REST_POST,
                             ((qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS),
                             restRequestCB, NULL, 0));
}

int InitDeleteRequest(OCQualityOfService qos)
{
  std::ostringstream query;
  query << coapServerResource;
  OIC_LOG_V(INFO, TAG, "\n\nExecuting %s", __func__);

  // First DELETE operation
  OCStackResult result = InvokeOCDoResource(query, OC_REST_DELETE,
                                            qos,
                                            restRequestCB, NULL, 0);
  if (OC_STACK_OK != result)
  {
    // Error can happen if for example, network connectivity is down
    OIC_LOG(INFO, TAG, "DELETE call did not succeed");
  }
  return result;
}

int InitGetRequest(OCQualityOfService qos)
{
  OIC_LOG_V(INFO, TAG, "\n\nExecuting %s", __func__);
  std::ostringstream query;
  query << coapServerResource;
  return (InvokeOCDoResource(query, OC_REST_GET,
                             (qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS, restRequestCB, NULL, 0));
}

int InitDiscovery(OCQualityOfService qos)
{
  OCCallbackData cbData;
  cbData.cb       = discoveryReqCB;
  cbData.context  = (void *)DEFAULT_CONTEXT_VALUE;
  cbData.cd       = NULL;

  OCDevAddr dest;
  dest.adapter    = OC_ADAPTER_REMOTE_ACCESS;
  dest.flags      = OC_DEFAULT_FLAGS;
  strncpy(dest.addr, remoteServerJabberID, MAX_ADDR_STR_SIZE - 1);

  OCStackResult ret = OCDoResource(NULL,
                                   OC_REST_GET,
                                   MULTICAST_RESOURCE_DISCOVERY_QUERY,
                                   &dest,
                                   NULL,
                                   CT_ADAPTER_REMOTE_ACCESS,
                                   (qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS,
                                   &cbData,
                                   NULL,
                                   0
                                  );

  if (ret != OC_STACK_OK)
  {
    OIC_LOG_V(ERROR, TAG, "Error %u in OCDoResource with discovery", ret);
  }
  return ret;
}

static void jidbound(char *jid)
{
  OIC_LOG_V(INFO, TAG, "\n\n    Bound JID: %s\n\n", jid);
}

int main(int argc, char *argv[])
{
  char host[] = "localhost";
  char user[] = "test1";
  char pass[] = "intel123";
  char empstr[] = "";
  OCRAInfo_t rainfo = {};

  rainfo.hostname = host;
  rainfo.port = 5222;
  rainfo.xmpp_domain = host;
  rainfo.username = user;
  rainfo.password = pass;
  rainfo.resource = empstr;
  rainfo.user_jid = empstr;
  rainfo.jidbound = jidbound;

  int opt = 0;
  while ((opt = getopt(argc, argv, "t:s:p:d:u:w:r:j:")) != -1)
  {
    switch (opt)
    {
      case 't':
        TEST_CASE = atoi(optarg);
        break;
      case 's':
        rainfo.hostname = optarg;
        break;
      case 'p':
        rainfo.port = atoi(optarg);
        break;
      case 'd':
        rainfo.xmpp_domain = optarg;
        break;
      case 'u':
        rainfo.username = optarg;
        break;
      case 'w':
        rainfo.password = optarg;
        break;
      case 'j':
        rainfo.user_jid = optarg;
        break;
      case 'r':
        rainfo.resource = optarg;
        break;
      default:
        PrintUsage();
        return -1;
    }
  }

  if (OCSetRAInfo(&rainfo) != OC_STACK_OK)
  {
    OIC_LOG(ERROR, TAG, "Error initiating remote access adapter");
    return 0;
  }

  if ((TEST_CASE < TEST_DISCOVER_REQ || TEST_CASE >= MAX_TESTS))
  {
    PrintUsage();
    return -1;
  }

  if (OCInit(NULL, 0, OC_CLIENT) != OC_STACK_OK)
  {
    OIC_LOG(ERROR, TAG, "OCStack init error");
    return 0;
  }

  OIC_LOG(INFO, TAG, "Enter JID of remote server");
  if (fgets(remoteServerJabberID, MAX_ADDR_STR_SIZE, stdin))
  {
    StripNewLineChar(remoteServerJabberID);
  }
  else
  {
    OIC_LOG(ERROR, TAG, "Bad input for jabberID");
    return OC_STACK_INVALID_PARAM;
  }

  InitDiscovery(OC_LOW_QOS);

  // Break from loop with Ctrl+C
  OIC_LOG(INFO, TAG, "Press CTRL+C to stop the stack");
  signal(SIGINT, handleSigInt);
  while (!gQuitFlag)
  {
    if (OCProcess() != OC_STACK_OK)
    {
      OIC_LOG(ERROR, TAG, "OCStack process error");
      return 0;
    }

    sleep(2);
  }
  OIC_LOG(INFO, TAG, "Exiting ocremoteaccessclient main loop...");

  if (OCStop() != OC_STACK_OK)
  {
    OIC_LOG(ERROR, TAG, "OCStack stop error");
  }

  return 0;
}
