import unittest

from .liteim_e2e import E2ETestCase, MessageType, TlvType, unique_name


class OfflineMessagesE2ETest(E2ETestCase):
    def test_offline_user_gets_private_message_after_login(self):
        sender_name = unique_name("sender")
        receiver_name = unique_name("receiver")

        with self.connect() as setup:
            register = setup.register(receiver_name, "secret", "Offline Receiver")
            receiver_id = register.uint64(TlvType.USER_ID)

        with self.connect() as sender:
            sender_id = sender.register_and_login(sender_name, "secret")
            response = sender.private_message(receiver_id, "offline hello from python e2e")
            self.assertEqual(response.msg_type, MessageType.PRIVATE_MESSAGE_RESPONSE)
            self.assertEqual(response.uint64(TlvType.SENDER_ID), sender_id)
            self.assertEqual(response.uint64(TlvType.RECEIVER_ID), receiver_id)

        with self.connect() as receiver:
            login = receiver.login(receiver_name, "secret")
            self.assertEqual(login.uint64(TlvType.USER_ID), receiver_id)
            offline = receiver.offline(limit=10)
            records = offline.message_records()
            self.assertIn("offline hello from python e2e", [record.text for record in records])
            message_ids = [record.message_id for record in records]

            still_pending = receiver.offline(limit=10)
            self.assertIn(
                "offline hello from python e2e",
                [record.text for record in still_pending.message_records()],
            )

            ack = receiver.offline_ack(message_ids)
            self.assertEqual(ack.msg_type, MessageType.OFFLINE_MESSAGES_ACK_RESPONSE)
            self.assertEqual(ack.uint64s(TlvType.MESSAGE_ID), message_ids)
            self.assertEqual(ack.uint64s(TlvType.DELIVERY_STATUS), [2] * len(message_ids))

            after_ack = receiver.offline(limit=10)
            self.assertNotIn(
                "offline hello from python e2e",
                [record.text for record in after_ack.message_records()],
            )


if __name__ == "__main__":
    unittest.main()
