"""Send the Groups disconnect datagrams from the UberDOG login operation.

Imported by the patched AstronLoginManagerUD. The field id is read from the
DC the UberDOG already loaded (otp.dc, toon.dc, platform.dc, groups.dc).
Each login gets its own nonce. The account channel is the same next time,
so the datagram and the remembered record both carry the nonce.
"""
import secrets

from direct.distributed.PyDatagram import PyDatagram

from .wire import avatar_gone_bytes, post_remove_bytes


def _send(air, blob):
    dg = PyDatagram()
    dg.appendData(blob)
    air.send(dg)


def avatar_gone_field_number(air):
    """dclass.getFieldByName('avatarGone').getNumber() from the loaded DC."""
    classes = getattr(air, 'dclassesByName', None) or {}
    for name in ('TTGroupsManagerUD', 'TTGroupsManager'):
        dclass = classes.get(name)
        if dclass is None:
            continue
        field = dclass.getFieldByName('avatarGone')
        if field is not None:
            return field.getNumber()
    raise RuntimeError('TTGroupsManager.avatarGone is not in the loaded DC')


def add_groups_post_remove(air, client_channel, av_id):
    """Register the notice next to the avatar DELETE_RAM post-remove.

    LoadAvatarOperation calls this with the account channel. That channel is
    the sender on the datagram. The nonce distinguishes this login from the
    next one on the same account channel.
    """
    client_channel = int(client_channel)
    av_id = int(av_id)
    nonce = secrets.randbits(32)
    field_number = avatar_gone_field_number(air)
    _send(air, post_remove_bytes(client_channel, int(air.ourChannel), av_id, field_number, nonce))
    mgr = getattr(air, 'groupsManager', None)
    if mgr is not None:
        mgr.remember_client(av_id, client_channel, nonce)
    return nonce


def send_groups_disconnect(air, client_channel, av_id):
    """Send the notice now. Unload clears post-removes before it deletes the avatar.

    The nonce is the one this login already registered. A fresh nonce would not match.
    """
    mgr = getattr(air, 'groupsManager', None)
    nonce = None if mgr is None else mgr.client_nonce(av_id)
    if nonce is None:
        return
    field_number = avatar_gone_field_number(air)
    _send(air, avatar_gone_bytes(int(client_channel), int(av_id), field_number, nonce))
