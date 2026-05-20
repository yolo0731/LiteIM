import contextlib
import dataclasses
import enum
import os
import random
import socket
import struct
import subprocess
import time
import unittest
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


PACKET_MAGIC = 0x4C494D31
PACKET_VERSION = 1
PACKET_FLAGS_NONE = 0
PACKET_HEADER = struct.Struct("!IBBHQI")
TLV_HEADER = struct.Struct("!HI")
MAX_PACKET_BODY = 1024 * 1024
SMALL_USER_ID_CONVERSATION_BASE = 10000
STRICT_E2E = os.environ.get("LITEIM_E2E_STRICT") == "1"


def _skip_or_fail(message: str) -> None:
    if STRICT_E2E:
        raise AssertionError(message)
    raise unittest.SkipTest(message)


class MessageType(enum.IntEnum):
    HEARTBEAT_REQUEST = 1
    HEARTBEAT_RESPONSE = 2
    REGISTER_REQUEST = 100
    REGISTER_RESPONSE = 101
    LOGIN_REQUEST = 102
    LOGIN_RESPONSE = 103
    ADD_FRIEND_REQUEST = 200
    ADD_FRIEND_RESPONSE = 201
    LIST_FRIENDS_REQUEST = 202
    LIST_FRIENDS_RESPONSE = 203
    PRIVATE_MESSAGE_REQUEST = 300
    PRIVATE_MESSAGE_RESPONSE = 301
    PRIVATE_MESSAGE_PUSH = 302
    CREATE_GROUP_REQUEST = 400
    CREATE_GROUP_RESPONSE = 401
    JOIN_GROUP_REQUEST = 402
    JOIN_GROUP_RESPONSE = 403
    LIST_GROUPS_REQUEST = 404
    LIST_GROUPS_RESPONSE = 405
    GROUP_MESSAGE_REQUEST = 406
    GROUP_MESSAGE_RESPONSE = 407
    GROUP_MESSAGE_PUSH = 408
    OFFLINE_MESSAGES_REQUEST = 500
    OFFLINE_MESSAGES_RESPONSE = 501
    HISTORY_REQUEST = 502
    HISTORY_RESPONSE = 503
    OFFLINE_MESSAGES_ACK_REQUEST = 504
    OFFLINE_MESSAGES_ACK_RESPONSE = 505
    DELIVERY_ACK_REQUEST = 506
    DELIVERY_ACK_RESPONSE = 507
    ERROR_RESPONSE = 900


class TlvType(enum.IntEnum):
    USERNAME = 1
    PASSWORD = 2
    USER_ID = 3
    NICKNAME = 4
    SESSION_ID = 6
    FRIEND_ID = 20
    TARGET_USER_ID = 21
    ONLINE_STATUS = 22
    GROUP_ID = 30
    GROUP_NAME = 31
    CONVERSATION_TYPE = 40
    CONVERSATION_ID = 41
    MESSAGE_ID = 42
    MESSAGE_TEXT = 43
    SENDER_ID = 44
    RECEIVER_ID = 45
    TIMESTAMP_MS = 46
    LIMIT = 48
    DELIVERY_STATUS = 50
    CLIENT_MESSAGE_ID = 51
    ERROR_CODE = 90
    ERROR_MESSAGE = 91


class ConversationType(enum.IntEnum):
    PRIVATE = 1
    GROUP = 2


@dataclasses.dataclass
class MessageRecord:
    message_id: int
    conversation_type: int
    conversation_id: int
    sender_id: int
    receiver_id: int
    text: str
    timestamp_ms: int


@dataclasses.dataclass
class Packet:
    msg_type: MessageType
    seq_id: int
    fields: Dict[int, List[bytes]]

    def values(self, tlv_type: TlvType) -> List[bytes]:
        return self.fields.get(int(tlv_type), [])

    def uint64s(self, tlv_type: TlvType) -> List[int]:
        result: List[int] = []
        for value in self.values(tlv_type):
            if len(value) != 8:
                raise AssertionError(f"{tlv_type.name} is not a uint64 field")
            result.append(struct.unpack("!Q", value)[0])
        return result

    def strings(self, tlv_type: TlvType) -> List[str]:
        return [value.decode("utf-8") for value in self.values(tlv_type)]

    def uint64(self, tlv_type: TlvType, index: int = 0) -> int:
        values = self.uint64s(tlv_type)
        if index >= len(values):
            raise AssertionError(f"missing uint64 field {tlv_type.name}")
        return values[index]

    def string(self, tlv_type: TlvType, index: int = 0) -> str:
        values = self.strings(tlv_type)
        if index >= len(values):
            raise AssertionError(f"missing string field {tlv_type.name}")
        return values[index]

    def message_records(self) -> List[MessageRecord]:
        ids = self.uint64s(TlvType.MESSAGE_ID)
        types = self.uint64s(TlvType.CONVERSATION_TYPE)
        conversations = self.uint64s(TlvType.CONVERSATION_ID)
        senders = self.uint64s(TlvType.SENDER_ID)
        receivers = self.uint64s(TlvType.RECEIVER_ID)
        texts = self.strings(TlvType.MESSAGE_TEXT)
        timestamps = self.uint64s(TlvType.TIMESTAMP_MS)
        count = len(ids)
        if not all(len(items) == count for items in (types, conversations, senders, receivers, texts, timestamps)):
            raise AssertionError("message TLV field counts do not match")
        return [
            MessageRecord(ids[i], types[i], conversations[i], senders[i], receivers[i], texts[i], timestamps[i])
            for i in range(count)
        ]


def unique_name(prefix: str) -> str:
    suffix = f"{int(time.time() * 1000)}_{os.getpid()}_{random.randint(0, 999999):06d}"
    return f"e2e_{prefix}_{suffix}"[:63]


def private_conversation_id(left_user_id: int, right_user_id: int) -> int:
    left = min(left_user_id, right_user_id)
    right = max(left_user_id, right_user_id)
    if left < SMALL_USER_ID_CONVERSATION_BASE and right < SMALL_USER_ID_CONVERSATION_BASE:
        return left * SMALL_USER_ID_CONVERSATION_BASE + right
    return (left << 32) | right


def _string_field(tlv_type: TlvType, value: str) -> Tuple[TlvType, bytes]:
    return tlv_type, value.encode("utf-8")


def _uint64_field(tlv_type: TlvType, value: int) -> Tuple[TlvType, bytes]:
    return tlv_type, struct.pack("!Q", value)


def _encode_body(fields: Iterable[Tuple[TlvType, bytes]]) -> bytes:
    body = bytearray()
    for tlv_type, value in fields:
        body += TLV_HEADER.pack(int(tlv_type), len(value))
        body += value
    return bytes(body)


def encode_packet(msg_type: MessageType, seq_id: int, fields: Iterable[Tuple[TlvType, bytes]]) -> bytes:
    body = _encode_body(fields)
    if len(body) > MAX_PACKET_BODY:
        raise ValueError("packet body is too large")
    return PACKET_HEADER.pack(
        PACKET_MAGIC,
        PACKET_VERSION,
        PACKET_FLAGS_NONE,
        int(msg_type),
        seq_id,
        len(body),
    ) + body


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data += chunk
    return bytes(data)


def decode_packet(sock: socket.socket) -> Packet:
    header_bytes = _recv_exact(sock, PACKET_HEADER.size)
    magic, version, _flags, raw_type, seq_id, body_len = PACKET_HEADER.unpack(header_bytes)
    if magic != PACKET_MAGIC:
        raise ValueError("invalid packet magic")
    if version != PACKET_VERSION:
        raise ValueError("invalid packet version")
    if body_len > MAX_PACKET_BODY:
        raise ValueError("packet body is too large")

    body = _recv_exact(sock, body_len) if body_len else b""
    fields: Dict[int, List[bytes]] = {}
    offset = 0
    while offset < len(body):
        if offset + TLV_HEADER.size > len(body):
            raise ValueError("incomplete TLV header")
        raw_tlv_type, value_len = TLV_HEADER.unpack(body[offset : offset + TLV_HEADER.size])
        offset += TLV_HEADER.size
        if offset + value_len > len(body):
            raise ValueError("incomplete TLV value")
        fields.setdefault(raw_tlv_type, []).append(body[offset : offset + value_len])
        offset += value_len

    return Packet(MessageType(raw_type), seq_id, fields)


class LiteIMServer:
    def __init__(self) -> None:
        self.server_bin = Path(os.environ.get("LITEIM_SERVER_BIN", "build/server/liteim_server"))
        self.host = os.environ.get("LITEIM_E2E_HOST", "127.0.0.1")
        self.port = int(os.environ.get("LITEIM_E2E_PORT", "9000"))
        self.process: Optional[subprocess.Popen[str]] = None

    def start(self) -> None:
        if os.environ.get("LITEIM_E2E_USE_EXISTING_SERVER") == "1":
            self._wait_for_port_or_skip()
            return

        if not self.server_bin.exists():
            _skip_or_fail(f"liteim_server was not found: {self.server_bin}")

        command = [str(self.server_bin)]
        config_path = os.environ.get("LITEIM_E2E_SERVER_CONFIG")
        if config_path:
            command.extend(["--config", config_path])

        self.process = subprocess.Popen(
            command,
            cwd=str(Path(__file__).resolve().parents[2]),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        self._wait_for_port_or_skip()

    def stop(self) -> None:
        if self.process is None:
            return
        if self.process.poll() is None:
            self.process.terminate()
            with contextlib.suppress(subprocess.TimeoutExpired):
                self.process.wait(timeout=5)
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=5)

    def _wait_for_port_or_skip(self) -> None:
        deadline = time.monotonic() + 8.0
        while time.monotonic() < deadline:
            if self.process is not None and self.process.poll() is not None:
                output = self.process.stdout.read() if self.process.stdout is not None else ""
                _skip_or_fail(f"liteim_server exited before accepting connections: {output}")
            try:
                with socket.create_connection((self.host, self.port), timeout=0.2):
                    return
            except OSError:
                time.sleep(0.05)
        _skip_or_fail("liteim_server did not become ready on the default E2E port")


class LiteIMClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 9000, rcvbuf: Optional[int] = None) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if rcvbuf is not None:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
        self.sock.settimeout(5.0)
        self.sock.connect((host, port))
        self.seq_id = 1
        self.pushes: List[Packet] = []

    def __enter__(self) -> "LiteIMClient":
        return self

    def __exit__(self, _exc_type, _exc, _tb) -> None:
        self.close()

    def close(self) -> None:
        with contextlib.suppress(OSError):
            self.sock.shutdown(socket.SHUT_RDWR)
        with contextlib.suppress(OSError):
            self.sock.close()

    def request(
        self,
        msg_type: MessageType,
        fields: Iterable[Tuple[TlvType, bytes]] = (),
        expected: Optional[MessageType] = None,
    ) -> Packet:
        seq_id = self.seq_id
        self.seq_id += 1
        self.sock.sendall(encode_packet(msg_type, seq_id, fields))
        while True:
            packet = decode_packet(self.sock)
            if packet.seq_id == seq_id:
                if expected is not None and packet.msg_type != expected:
                    raise AssertionError(
                        f"expected {expected.name}, got {packet.msg_type.name}: {packet.fields}"
                    )
                return packet
            self.pushes.append(packet)

    def expect_push(self, msg_type: MessageType, timeout: float = 5.0) -> Packet:
        for index, packet in enumerate(self.pushes):
            if packet.msg_type == msg_type:
                return self.pushes.pop(index)

        previous_timeout = self.sock.gettimeout()
        self.sock.settimeout(timeout)
        try:
            while True:
                packet = decode_packet(self.sock)
                if packet.msg_type == msg_type:
                    return packet
                self.pushes.append(packet)
        finally:
            self.sock.settimeout(previous_timeout)

    def register(self, username: str, password: str, nickname: str = "") -> Packet:
        fields = [
            _string_field(TlvType.USERNAME, username),
            _string_field(TlvType.PASSWORD, password),
        ]
        if nickname:
            fields.append(_string_field(TlvType.NICKNAME, nickname))
        return self.request(MessageType.REGISTER_REQUEST, fields, MessageType.REGISTER_RESPONSE)

    def login(
        self,
        username: str,
        password: str,
        expected: MessageType = MessageType.LOGIN_RESPONSE,
    ) -> Packet:
        return self.request(
            MessageType.LOGIN_REQUEST,
            [_string_field(TlvType.USERNAME, username), _string_field(TlvType.PASSWORD, password)],
            expected,
        )

    def register_and_login(self, username: str, password: str) -> int:
        self.register(username, password, username)
        login = self.login(username, password)
        return login.uint64(TlvType.USER_ID)

    def heartbeat(self) -> Packet:
        return self.request(MessageType.HEARTBEAT_REQUEST, expected=MessageType.HEARTBEAT_RESPONSE)

    def add_friend(self, user_id: int) -> Packet:
        return self.request(
            MessageType.ADD_FRIEND_REQUEST,
            [_uint64_field(TlvType.TARGET_USER_ID, user_id)],
            MessageType.ADD_FRIEND_RESPONSE,
        )

    def list_friends(self) -> Packet:
        return self.request(MessageType.LIST_FRIENDS_REQUEST, expected=MessageType.LIST_FRIENDS_RESPONSE)

    def private_message(
        self,
        receiver_id: int,
        text: str,
        client_message_id: str = "",
        expected: Optional[MessageType] = MessageType.PRIVATE_MESSAGE_RESPONSE,
    ) -> Packet:
        fields = [
            _uint64_field(TlvType.RECEIVER_ID, receiver_id),
            _string_field(TlvType.MESSAGE_TEXT, text),
        ]
        if client_message_id:
            fields.append(_string_field(TlvType.CLIENT_MESSAGE_ID, client_message_id))
        return self.request(
            MessageType.PRIVATE_MESSAGE_REQUEST,
            fields,
            expected,
        )

    def create_group(self, name: str) -> int:
        response = self.request(
            MessageType.CREATE_GROUP_REQUEST,
            [_string_field(TlvType.GROUP_NAME, name)],
            MessageType.CREATE_GROUP_RESPONSE,
        )
        return response.uint64(TlvType.GROUP_ID)

    def join_group(self, group_id: int) -> Packet:
        return self.request(
            MessageType.JOIN_GROUP_REQUEST,
            [_uint64_field(TlvType.GROUP_ID, group_id)],
            MessageType.JOIN_GROUP_RESPONSE,
        )

    def group_message(self, group_id: int, text: str) -> Packet:
        return self.request(
            MessageType.GROUP_MESSAGE_REQUEST,
            [_uint64_field(TlvType.GROUP_ID, group_id), _string_field(TlvType.MESSAGE_TEXT, text)],
            MessageType.GROUP_MESSAGE_RESPONSE,
        )

    def history_private(self, conversation_id: int, limit: int = 20) -> Packet:
        return self._history(ConversationType.PRIVATE, conversation_id, limit)

    def history_group(self, group_id: int, limit: int = 20) -> Packet:
        return self._history(ConversationType.GROUP, group_id, limit)

    def _history(self, conversation_type: ConversationType, conversation_id: int, limit: int) -> Packet:
        return self.request(
            MessageType.HISTORY_REQUEST,
            [
                _uint64_field(TlvType.CONVERSATION_TYPE, int(conversation_type)),
                _uint64_field(TlvType.CONVERSATION_ID, conversation_id),
                _uint64_field(TlvType.LIMIT, limit),
            ],
            MessageType.HISTORY_RESPONSE,
        )

    def offline(self, limit: int = 20) -> Packet:
        return self.request(
            MessageType.OFFLINE_MESSAGES_REQUEST,
            [_uint64_field(TlvType.LIMIT, limit)],
            MessageType.OFFLINE_MESSAGES_RESPONSE,
        )

    def offline_ack(self, message_ids: Iterable[int]) -> Packet:
        return self.request(
            MessageType.OFFLINE_MESSAGES_ACK_REQUEST,
            [_uint64_field(TlvType.MESSAGE_ID, message_id) for message_id in message_ids],
            MessageType.OFFLINE_MESSAGES_ACK_RESPONSE,
        )

    def delivery_ack(
        self,
        message_id: int,
        expected: MessageType = MessageType.DELIVERY_ACK_RESPONSE,
    ) -> Packet:
        return self.request(
            MessageType.DELIVERY_ACK_REQUEST,
            [_uint64_field(TlvType.MESSAGE_ID, message_id)],
            expected,
        )

    def closed_by_peer(self, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        previous_timeout = self.sock.gettimeout()
        self.sock.settimeout(0.05)
        try:
            while time.monotonic() < deadline:
                try:
                    chunk = self.sock.recv(4096)
                except socket.timeout:
                    continue
                except OSError:
                    return True
                if not chunk:
                    return True
            return False
        finally:
            self.sock.settimeout(previous_timeout)


class E2ETestCase(unittest.TestCase):
    server: LiteIMServer

    @classmethod
    def setUpClass(cls) -> None:
        cls.server = LiteIMServer()
        cls.server.start()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.stop()

    def connect(self, rcvbuf: Optional[int] = None) -> LiteIMClient:
        return LiteIMClient(self.server.host, self.server.port, rcvbuf=rcvbuf)
