import unittest

from .liteim_e2e import E2ETestCase, MessageType, unique_name


class HeartbeatE2ETest(E2ETestCase):
    def test_unauthenticated_and_authenticated_heartbeat_return_success(self):
        with self.connect() as client:
            unauthenticated = client.heartbeat()
            self.assertEqual(unauthenticated.msg_type, MessageType.HEARTBEAT_RESPONSE)

            client.register_and_login(unique_name("heartbeat"), "secret")
            authenticated = client.heartbeat()
            self.assertEqual(authenticated.msg_type, MessageType.HEARTBEAT_RESPONSE)


if __name__ == "__main__":
    unittest.main()
