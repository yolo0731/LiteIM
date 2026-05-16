import unittest

from .liteim_e2e import E2ETestCase, MessageType, TlvType, unique_name


class BackpressureE2ETest(E2ETestCase):
    def test_slow_online_receiver_is_closed_by_backpressure(self):
        sender_name = unique_name("fast")
        receiver_name = unique_name("slow")

        with self.connect() as sender, self.connect(rcvbuf=4096) as slow_receiver:
            sender.register_and_login(sender_name, "secret")
            receiver_id = slow_receiver.register_and_login(receiver_name, "secret")

            payload = "x" * 60000
            for index in range(90):
                response = sender.private_message(receiver_id, f"{index}:{payload}", expected=None)
                self.assertIn(
                    response.msg_type,
                    {MessageType.PRIVATE_MESSAGE_RESPONSE, MessageType.ERROR_RESPONSE},
                )

            self.assertTrue(slow_receiver.closed_by_peer(timeout=5.0))


if __name__ == "__main__":
    unittest.main()
