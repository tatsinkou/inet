//
// Copyright (C) 2018 OpenSim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#include "inet/networklayer/ipv4/Ipv4Dissector.h"

#include "inet/common/ProtocolTag_m.h"
#include "inet/common/packet/dissector/ProtocolDissectorRegistry.h"
#include "inet/networklayer/ipv4/Ipv4.h"

namespace inet {

Register_Protocol_Dissector(&Protocol::ipv4, Ipv4Dissector);

void Ipv4Dissector::dissect(Packet *packet, ICallback& callback) const
{
    auto trailerPopOffset = packet->getTrailerPopOffset();
    auto ipv4EndOffset = packet->getHeaderPopOffset();
    const auto& header = packet->popHeader<Ipv4Header>();
    ipv4EndOffset += B(header->getTotalLengthField());
    callback.startProtocolDataUnit(&Protocol::ipv4);
    callback.visitChunk(header, &Protocol::ipv4);
    packet->setTrailerPopOffset(ipv4EndOffset);
    if (header->getFragmentOffset() == 0 && !header->getMoreFragments()) {
        callback.dissectPacket(packet, header->getProtocol());
    }
    else {
        //TODO Fragmentation
        callback.dissectPacket(packet, nullptr);
    }
    ASSERT(packet->getDataLength() == B(0));
    packet->setHeaderPopOffset(ipv4EndOffset);
    packet->setTrailerPopOffset(trailerPopOffset);
    callback.endProtocolDataUnit(&Protocol::ipv4);
}

} // namespace inet

