/*
 * Copyright 2014 CyberVision, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "kaa_platform_protocol.h"
#include "utilities/kaa_mem.h"
#include "utilities/kaa_log.h"
#include "kaa_context.h"
#include "kaa_defaults.h"
#include "kaa_status.h"

#include "kaa_event.h"
#include "kaa_profile.h"
#include "kaa_logging.h"
#include "kaa_user.h"

#include "gen/kaa_endpoint_gen.h"

/** External user manager API */
extern kaa_error_t kaa_user_manager_handle_sync(kaa_user_manager_t *self
        , kaa_user_attach_response_t * user_attach_response, kaa_user_attach_notification_t *attach, kaa_user_detach_notification_t *detach);
extern kaa_error_t kaa_user_compile_request(kaa_user_manager_t *self, kaa_user_sync_request_t** request_p, size_t requestId);

/** External event manager API */
extern kaa_error_t kaa_event_compile_request(kaa_event_manager_t *self, kaa_event_sync_request_t** request_p, size_t requestId);
extern kaa_error_t kaa_event_handle_sync(kaa_event_manager_t *self, size_t request_id, kaa_event_sequence_number_response_t *event_sn_response, kaa_list_t *events);

/** External profile API */
extern kaa_error_t kaa_profile_compile_request(kaa_profile_manager_t *kaa_context, kaa_profile_sync_request_t **result);
extern kaa_error_t kaa_profile_need_profile_resync(kaa_profile_manager_t *kaa_context, bool *result);
extern kaa_error_t kaa_profile_handle_sync(kaa_profile_manager_t *kaa_context, kaa_profile_sync_response_t *profile);

/** External logging API */
extern kaa_error_t kaa_logging_compile_request(kaa_log_collector_t *self, kaa_log_sync_request_t **result);
extern kaa_error_t kaa_logging_handle_sync(kaa_log_collector_t *self, kaa_log_sync_response_t *response);


struct kaa_platform_protocol_t
{
    kaa_context_t *kaa_context;
    uint32_t       request_id;
    kaa_logger_t  *logger;
};


static kaa_sync_request_meta_data_t* create_sync_request_meta_data(kaa_context_t *context)
{
    kaa_sync_request_meta_data_t *request = kaa_sync_request_meta_data_create();
    request->application_token = kaa_string_move_create(APPLICATION_TOKEN, NULL);
    request->timeout = 60000L;

    kaa_digest_p pub_key_hash = NULL;
    if (kaa_status_get_endpoint_public_key_hash(context->status, &pub_key_hash)) {
        // FIXME: error handling
    }
    if (pub_key_hash) {
        request->endpoint_public_key_hash =
                kaa_bytes_copy_create(pub_key_hash, SHA_1_DIGEST_LENGTH, kaa_data_destroy);
    }

    kaa_digest_p profile_hash = NULL;
    if (kaa_status_get_profile_hash(context->status, &profile_hash)) {
        // FIXME: error handling
    }

    if (profile_hash) {
        request->profile_hash = kaa_union_bytes_or_null_branch_0_create();
        if (request->profile_hash) {
            request->profile_hash->data = kaa_bytes_copy_create(
                    profile_hash, SHA_1_DIGEST_LENGTH, kaa_data_destroy);
        }
    } else {
        request->profile_hash = kaa_union_bytes_or_null_branch_1_create();
    }

    return request;
}


static kaa_error_t kaa_compile_request(kaa_platform_protocol_t *self, kaa_sync_request_t **request_p
        , size_t *result_size, size_t service_count, const kaa_service_t services[])
{
    kaa_sync_request_t *request = kaa_sync_request_create();
    KAA_RETURN_IF_NIL(request, KAA_ERR_NOMEM);

    request->request_id = kaa_union_int_or_null_branch_0_create();
    request->request_id->data = (uint32_t *) KAA_MALLOC(sizeof(uint32_t));
    *((uint32_t *)request->request_id->data) = self->request_id;

    request->sync_request_meta_data = kaa_union_sync_request_meta_data_or_null_branch_0_create();
    request->sync_request_meta_data->data = create_sync_request_meta_data(self->kaa_context);

    request->user_sync_request = kaa_union_user_sync_request_or_null_branch_0_create();
    kaa_user_compile_request(self->kaa_context->user_manager
                           , (kaa_user_sync_request_t **)&request->user_sync_request->data
                           , self->request_id);

    request->event_sync_request = kaa_union_event_sync_request_or_null_branch_1_create();
    request->log_sync_request = kaa_union_log_sync_request_or_null_branch_1_create();
    request->notification_sync_request = kaa_union_notification_sync_request_or_null_branch_1_create();
    request->configuration_sync_request = kaa_union_configuration_sync_request_or_null_branch_1_create();
    request->profile_sync_request = kaa_union_profile_sync_request_or_null_branch_1_create();

    for (;service_count--;) {
        switch (services[service_count]) {
#ifndef KAA_DISABLE_FEATURE_EVENTS
        case KAA_SERVICE_EVENT: {
            request->event_sync_request->destroy(request->event_sync_request);
            request->event_sync_request = kaa_union_event_sync_request_or_null_branch_0_create();
            kaa_event_compile_request(self->kaa_context->event_manager
                                    , (kaa_event_sync_request_t**)&request->event_sync_request->data
                                    , self->request_id);
            break;
        }
#endif
        case KAA_SERVICE_PROFILE: {
            bool need_resync = false;
            kaa_error_t error = kaa_profile_need_profile_resync(self->kaa_context->profile_manager, &need_resync);
            if (error) {
                request->destroy(request);
                return error;
            }

            if (need_resync) {
                request->profile_sync_request->destroy(request->profile_sync_request);
                request->profile_sync_request = kaa_union_profile_sync_request_or_null_branch_0_create();

                if (!request->profile_sync_request) {
                    request->destroy(request);
                    return KAA_ERR_NOMEM;
                }

                error = kaa_profile_compile_request(self->kaa_context->profile_manager
                        , (kaa_profile_sync_request_t **)&request->profile_sync_request->data);

                if (error) {
                    request->destroy(request);
                    return error;
                }
            }
            break;
        }
#ifndef KAA_DISABLE_FEATURE_LOGGING
        case KAA_SERVICE_LOGGING: {
            kaa_log_sync_request_t *log_request = NULL;
            kaa_logging_compile_request(self->kaa_context->log_collector, &log_request);
            if (log_request) {
                request->log_sync_request->destroy(request->log_sync_request);
                request->log_sync_request =
                        kaa_union_log_sync_request_or_null_branch_0_create();
                request->log_sync_request->data = log_request;
            }
            break;
        }
#endif
        default:
            break;
        }
    }

    *request_p = request;
    *result_size = request->get_size(request);
    return KAA_ERR_NONE;
}


static kaa_error_t kaa_serialize_request(kaa_sync_request_t *request, const char *buffer, size_t request_size)
{
    KAA_RETURN_IF_NIL2(request, buffer, KAA_ERR_BADPARAM);
    avro_writer_t writer = avro_writer_memory(buffer, request_size);
    request->serialize(writer, request);
    avro_writer_free(writer);
    return KAA_ERR_NONE;
}


kaa_error_t kaa_platform_protocol_create(kaa_platform_protocol_t **platform_protocol_p, kaa_context_t *context, kaa_logger_t *logger)
{
    KAA_RETURN_IF_NIL2(platform_protocol_p, context, KAA_ERR_BADPARAM);

    *platform_protocol_p = KAA_MALLOC(sizeof(kaa_platform_protocol_t));
    KAA_RETURN_IF_NIL(*platform_protocol_p, KAA_ERR_NOMEM);

    (*platform_protocol_p)->request_id = 0;
    (*platform_protocol_p)->kaa_context = context;
    return KAA_ERR_NONE;
}



void kaa_platform_protocol_destroy(kaa_platform_protocol_t *self)
{
    if (self)
        KAA_FREE(self);
}



kaa_error_t kaa_platform_protocol_serialize_client_sync(kaa_platform_protocol_t *self
        , const kaa_service_t services[], size_t services_count
        , kaa_buffer_alloc_fn allocator, void *allocator_context)
{
    KAA_RETURN_IF_NIL4(self, services, services_count, allocator, KAA_ERR_BADPARAM);

    kaa_sync_request_t *sync_request = NULL;
    size_t buffer_size = 0;
    self->request_id++;
    kaa_error_t error = kaa_compile_request(self, &sync_request, &buffer_size, services_count, services);
    if (!error) {
        const char *buffer = allocator(allocator_context, buffer_size);
        if (buffer) {
            error = kaa_serialize_request(sync_request, buffer, buffer_size);
        } else {
            error = KAA_ERR_WRITE_FAILED;
        }
        sync_request->destroy(sync_request);
    }

    return error;
}



kaa_error_t kaa_platform_protocol_process_server_sync(kaa_platform_protocol_t *self
        , const char *buffer, size_t buffer_size)
{
    KAA_RETURN_IF_NIL3(self, buffer, buffer_size, KAA_ERR_BADPARAM);

    avro_reader_t reader = avro_reader_memory(buffer, buffer_size);
    kaa_sync_response_t * response = kaa_sync_response_deserialize(reader);
    avro_reader_free(reader);

#ifndef KAA_DISABLE_FEATURE_EVENTS
    uint32_t responseId =
            response->request_id != NULL && response->request_id->type == KAA_UNION_INT_OR_NULL_BRANCH_0
                    ? *((uint32_t*)response->request_id->data)
                    : 0;
    kaa_list_t * received_events = NULL;
    kaa_event_sequence_number_response_t * event_sn_response = NULL;
    if (response->event_sync_response != NULL) {
        if (response->event_sync_response->type == KAA_UNION_EVENT_SYNC_RESPONSE_OR_NULL_BRANCH_0) {
            kaa_event_sync_response_t * ev_response = response->event_sync_response->data;
            if (ev_response != NULL && ev_response->events != NULL && ev_response->events->type == KAA_UNION_ARRAY_EVENT_OR_NULL_BRANCH_0) {
                received_events = (kaa_list_t *)ev_response->events->data;
            }
            if (ev_response->event_sequence_number_response != NULL
                    && ev_response->event_sequence_number_response->type == KAA_UNION_EVENT_SEQUENCE_NUMBER_RESPONSE_OR_NULL_BRANCH_0) {
                event_sn_response = (kaa_event_sequence_number_response_t *) ev_response->event_sequence_number_response->data;
            }
        }
    }
    kaa_event_handle_sync(self->kaa_context->event_manager, responseId, event_sn_response, received_events);
#endif
    if (response->user_sync_response != NULL) {
        if (response->user_sync_response->type == KAA_UNION_USER_SYNC_RESPONSE_OR_NULL_BRANCH_0) {
            kaa_user_sync_response_t * usr_response = response->user_sync_response->data;
            if (usr_response != NULL) {
                kaa_user_attach_response_t *     usr_attach_response = NULL;
                kaa_user_attach_notification_t * usr_attach_notif = NULL;
                kaa_user_detach_notification_t * usr_detach_notif = NULL;
                if (usr_response->user_attach_response != NULL
                        && usr_response->user_attach_response->type == KAA_UNION_USER_ATTACH_RESPONSE_OR_NULL_BRANCH_0)
                {
                    usr_attach_response = usr_response->user_attach_response->data;
                }
                if(usr_response->user_attach_notification != NULL
                        && usr_response->user_attach_notification->type == KAA_UNION_USER_ATTACH_NOTIFICATION_OR_NULL_BRANCH_0)
                {
                    usr_attach_notif = usr_response->user_attach_notification->data;
                }
                if (usr_response->user_detach_notification != NULL
                        && usr_response->user_detach_notification->type == KAA_UNION_USER_DETACH_NOTIFICATION_OR_NULL_BRANCH_0)
                {
                    usr_detach_notif = usr_response->user_detach_notification->data;
                }
                kaa_user_manager_handle_sync(self->kaa_context->user_manager
                        , usr_attach_response, usr_attach_notif, usr_detach_notif);
            }
        }
    }

    if (response->profile_sync_response != NULL
            && response->profile_sync_response->type == KAA_UNION_PROFILE_SYNC_REQUEST_OR_NULL_BRANCH_0) {
        kaa_profile_handle_sync(self->kaa_context->profile_manager, (kaa_profile_sync_response_t *)response->profile_sync_response->data);
    }

#ifndef KAA_DISABLE_FEATURE_LOGGING
    if (response->log_sync_response != NULL
            && response->log_sync_response->type == KAA_UNION_LOG_SYNC_REQUEST_OR_NULL_BRANCH_0) {
        kaa_logging_handle_sync(self->kaa_context->log_collector, (kaa_log_sync_response_t *)response->log_sync_response->data);
    }
#endif

    kaa_status_save(self->kaa_context->status);
    response->destroy(response);

    return KAA_ERR_NONE;
}