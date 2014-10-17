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
package org.kaaproject.kaa.server.operations.service.akka.actors.core;

import io.netty.channel.ChannelHandlerContext;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import org.kaaproject.kaa.common.TransportType;
import org.kaaproject.kaa.server.operations.pojo.SyncResponseHolder;
import org.kaaproject.kaa.server.operations.service.akka.messages.core.endpoint.SyncRequestMessage;
import org.kaaproject.kaa.server.operations.service.http.commands.ChannelType;
import org.kaaproject.kaa.server.operations.service.netty.NettySessionInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ChannelMap {

    /** The Constant LOG. */
    private static final Logger LOG = LoggerFactory.getLogger(ChannelMap.class);

    private final String endpointKey;
    private final String actorKey;
    private final Map<UUID, ChannelMetaData> map;

    protected ChannelMap(String endpointKey, String actorKey) {
        super();
        this.endpointKey = endpointKey;
        this.actorKey = actorKey;
        this.map = new HashMap<>();
    }

    public ChannelMetaData getById(UUID id){
        return this.map.get(id);
    }

    public ChannelMetaData getByRequestId(UUID id){
        for(ChannelMetaData data : this.map.values()){
            if(data.getRequest().getChannelUuid().equals(id)){
                return data;
            }
        }
        return null;
    }

    public void addChannel(ChannelMetaData data){
        this.map.put(data.getId(), data);
        LOG.debug("[{}][{}] Added new channel {} to the map. Channel map size: {}", endpointKey, actorKey, data, map.size());
    }

    public void removeChannel(ChannelMetaData channel) {
        this.map.remove(channel.getId());
        LOG.debug("[{}][{}] Removed channel [{}] from the map. Channel map size: {}", endpointKey, actorKey, channel.getId(), map.size());
    }

    public List<ChannelMetaData> getByTransportType(TransportType type) {
        List<ChannelMetaData> result = new ArrayList<>();
        for(ChannelMetaData data : map.values()){
            if(data.getRequest().isValid(type)){
                result.add(data);
            }
        }
        return result;
    }

    static final class ChannelMetaData{
        private final NettySessionInfo session;
        SyncRequestMessage request;
        SyncResponseHolder response;
        private long lastActivityTime;

        public ChannelMetaData(SyncRequestMessage request){
            this(request, null);
        }

        public ChannelMetaData(SyncRequestMessage request, SyncResponseHolder response) {
            super();
            this.session = request.getSession();
            this.request = request;
            this.response = response;
        }

        public void updateRequest(SyncRequestMessage syncRequest){
            this.request.update(syncRequest);
        }

        public void updateReqResp(SyncResponseHolder response){
            cleanRequest();
            this.response = response;
        }

        public UUID getId() {
            return session.getUuid();
        }

        public ChannelType getType() {
            return session.getChannelType();
        }

        public ChannelHandlerContext getContext() {
            return session.getCtx();
        }

        public int getKeepAlive() {
            return session.getKeepAlive();
        }

        public SyncRequestMessage getRequest() {
            return request;
        }

        public SyncResponseHolder getResponse() {
            return response;
        }

        public long getLastActivityTime() {
            return lastActivityTime;
        }

        public void setLastActivityTime(long lastActivityTime) {
            this.lastActivityTime = lastActivityTime;
        }

        public void cleanRequest() {
            request.cleanRequest();
        }

        @Override
        public int hashCode() {
            final int prime = 31;
            int result = 1;
            result = prime * result + ((session == null) ? 0 : session.hashCode());
            return result;
        }

        @Override
        public boolean equals(Object obj) {
            if (this == obj) {
                return true;
            }
            if (obj == null) {
                return false;
            }
            if (getClass() != obj.getClass()) {
                return false;
            }
            ChannelMetaData other = (ChannelMetaData) obj;
            if (session == null) {
                if (other.session != null) {
                    return false;
                }
            } else if (!session.equals(other.session)) {
                return false;
            }
            return true;
        }

        @Override
        public String toString() {
            StringBuilder builder = new StringBuilder();
            builder.append("ChannelMetaData [session=");
            builder.append(session);
            builder.append("]");
            return builder.toString();
        }



    }

    public boolean isEmpty() {
        return map.isEmpty();
    }
}