"""Astron datagrams for Groups, with no Panda.

The login operation registers one of these as a CLIENTAGENT_ADD_POST_REMOVE.
On disconnect the client agent delivers the inner field update to the Groups
manager (DoId 4722). That still runs after the avatar has moved to a district,
which is when the UberDOG copy of the avatar is already gone.

The field id in the body is the DC wire id from a DCFile that has read
otp.dc, toon.dc, platform.dc and groups.dc. It is not the class-local index.

Server header layout matches direct.distributed.PyDatagram.addServerHeader.
"""
import struct

from . import groups as G

CLIENTAGENT_ADD_POST_REMOVE = 1110
STATESERVER_OBJECT_SET_FIELD = 2020
_HEADER = struct.Struct('<BQQH')
_HEADER_LEN = _HEADER.size  # 19


def server_header(channel, sender, code):
    return _HEADER.pack(1, int(channel), int(sender), int(code))


def parse_header(blob):
    if len(blob) < _HEADER_LEN:
        return None
    count, channel, sender, code = _HEADER.unpack_from(blob, 0)
    if count != 1:
        return None
    return channel, sender, code, blob[_HEADER_LEN:]


def avatar_gone_bytes(client_channel, av_id, field_number, nonce):
    """STATESERVER_OBJECT_SET_FIELD avatarGone(avId, nonce), addressed to DoId 4722.

    field_number is dclass.getFieldByName('avatarGone').getNumber().
    nonce is the per-login value stored with the account channel. A later login
    on the same account channel uses a new nonce, so this datagram cannot evict it.
    """
    field_number = int(field_number)
    body = struct.pack('<IHII', G.GROUPS_MANAGER_DO_ID, field_number, int(av_id), int(nonce) & 0xFFFFFFFF)
    return server_header(G.GROUPS_MANAGER_DO_ID, int(client_channel), STATESERVER_OBJECT_SET_FIELD) + body


def post_remove_bytes(client_channel, air_channel, av_id, field_number, nonce):
    """CLIENTAGENT_ADD_POST_REMOVE wrapping avatar_gone_bytes."""
    inner = avatar_gone_bytes(client_channel, av_id, field_number, nonce)
    return server_header(int(client_channel), int(air_channel), CLIENTAGENT_ADD_POST_REMOVE) + struct.pack('<H', len(inner)) + inner


def unwrap_post_remove(blob):
    parsed = parse_header(blob)
    if parsed is None:
        return None
    _channel, _sender, code, rest = parsed
    if code != CLIENTAGENT_ADD_POST_REMOVE or len(rest) < 2:
        return None
    length = struct.unpack_from('<H', rest, 0)[0]
    inner = rest[2:2 + length]
    if len(inner) != length:
        return None
    return inner


def parse_field_update(blob):
    """Return (channel, sender, do_id, field_id, payload) for a SET_FIELD, else None."""
    parsed = parse_header(blob)
    if parsed is None:
        return None
    channel, sender, code, rest = parsed
    if code != STATESERVER_OBJECT_SET_FIELD or len(rest) < 6:
        return None
    do_id, field_id = struct.unpack_from('<IH', rest, 0)
    return channel, sender, do_id, field_id, rest[6:]
