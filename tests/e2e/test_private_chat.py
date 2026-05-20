import unittest

from .liteim_e2e import E2ETestCase, MessageType, TlvType, private_conversation_id, unique_name


class PrivateChatE2ETest(E2ETestCase):
    def test_two_logged_in_users_can_private_chat_and_read_history(self):
        alice_name = unique_name("alice")
        bob_name = unique_name("bob")

        with self.connect() as alice, self.connect() as bob:
            alice_id = alice.register_and_login(alice_name, "secret")
            bob_id = bob.register_and_login(bob_name, "secret")
            alice.add_friend(bob_id)

            response = alice.private_message(bob_id, "hello bob from python e2e")
            self.assertEqual(response.msg_type, MessageType.PRIVATE_MESSAGE_RESPONSE)
            self.assertEqual(response.uint64(TlvType.SENDER_ID), alice_id)
            self.assertEqual(response.uint64(TlvType.RECEIVER_ID), bob_id)

            push = bob.expect_push(MessageType.PRIVATE_MESSAGE_PUSH)
            self.assertEqual(push.uint64(TlvType.SENDER_ID), alice_id)
            self.assertEqual(push.uint64(TlvType.RECEIVER_ID), bob_id)
            self.assertEqual(push.string(TlvType.MESSAGE_TEXT), "hello bob from python e2e")

            history = alice.history_private(private_conversation_id(alice_id, bob_id), limit=10)
            texts = [record.text for record in history.message_records()]
            self.assertIn("hello bob from python e2e", texts)

    def test_duplicate_client_message_id_returns_existing_message_once(self):
        alice_name = unique_name("alice_idem")
        bob_name = unique_name("bob_idem")
        text = "idempotent private hello from python e2e"
        client_message_id = unique_name("client_msg")

        with self.connect() as setup:
            register = setup.register(bob_name, "secret", "Idempotent Bob")
            bob_id = register.uint64(TlvType.USER_ID)

        with self.connect() as alice:
            alice_id = alice.register_and_login(alice_name, "secret")
            first = alice.private_message(bob_id, text, client_message_id=client_message_id)
            duplicate = alice.private_message(bob_id, text, client_message_id=client_message_id)

            self.assertEqual(first.msg_type, MessageType.PRIVATE_MESSAGE_RESPONSE)
            self.assertEqual(duplicate.msg_type, MessageType.PRIVATE_MESSAGE_RESPONSE)
            self.assertEqual(first.uint64(TlvType.SENDER_ID), alice_id)
            self.assertEqual(first.uint64(TlvType.MESSAGE_ID), duplicate.uint64(TlvType.MESSAGE_ID))
            self.assertEqual(first.string(TlvType.CLIENT_MESSAGE_ID), client_message_id)
            self.assertEqual(duplicate.string(TlvType.CLIENT_MESSAGE_ID), client_message_id)

        with self.connect() as bob:
            bob.login(bob_name, "secret")
            offline = bob.offline(limit=10)
            matching = [record for record in offline.message_records() if record.text == text]
            self.assertEqual(len(matching), 1)
            self.assertEqual(matching[0].message_id, first.uint64(TlvType.MESSAGE_ID))


if __name__ == "__main__":
    unittest.main()
