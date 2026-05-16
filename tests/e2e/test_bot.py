import unittest

from .liteim_e2e import E2ETestCase, MessageType, TlvType, unique_name


class BotE2ETest(E2ETestCase):
    def test_private_message_to_mira_bot_returns_echo_push(self):
        with self.connect() as alice:
            alice_id = alice.register_and_login(unique_name("bot_alice"), "secret")

            response = alice.private_message(9001, "hello mira from e2e")
            self.assertEqual(response.msg_type, MessageType.PRIVATE_MESSAGE_RESPONSE)
            self.assertEqual(response.uint64(TlvType.SENDER_ID), alice_id)
            self.assertEqual(response.uint64(TlvType.RECEIVER_ID), 9001)

            push = alice.expect_push(MessageType.PRIVATE_MESSAGE_PUSH, timeout=2.0)
            self.assertEqual(push.uint64(TlvType.SENDER_ID), 9001)
            self.assertEqual(push.uint64(TlvType.RECEIVER_ID), alice_id)
            self.assertIn("Echo:", push.string(TlvType.MESSAGE_TEXT))


if __name__ == "__main__":
    unittest.main()
