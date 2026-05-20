import os
import tempfile
import unittest

from .liteim_e2e import E2ETestCase, MessageType, unique_name


class BackpressureE2ETest(E2ETestCase):
    config_path = ""

    @classmethod
    def setUpClass(cls):
        with tempfile.NamedTemporaryFile("w", delete=False) as config:
            config.write("server.output_high_water_mark_bytes = 32768\n")
            cls.config_path = config.name
        os.environ["LITEIM_E2E_SERVER_CONFIG"] = cls.config_path
        super().setUpClass()

    @classmethod
    def tearDownClass(cls):
        try:
            super().tearDownClass()
        finally:
            os.environ.pop("LITEIM_E2E_SERVER_CONFIG", None)
            if cls.config_path:
                try:
                    os.unlink(cls.config_path)
                except OSError:
                    pass

    def test_slow_online_receiver_is_closed_by_backpressure(self):
        sender_name = unique_name("fast")
        receiver_name = unique_name("slow")

        with self.connect() as sender, self.connect(rcvbuf=4096) as slow_receiver:
            sender_id = sender.register_and_login(sender_name, "secret")
            receiver_id = slow_receiver.register_and_login(receiver_name, "secret")
            sender.add_friend(receiver_id)
            slow_receiver.accept_friend(sender_id)

            payload = "x" * 7900
            for index in range(700):
                response = sender.private_message(receiver_id, f"{index}:{payload}", expected=None)
                self.assertIn(
                    response.msg_type,
                    {MessageType.PRIVATE_MESSAGE_RESPONSE, MessageType.ERROR_RESPONSE},
                )

            self.assertTrue(slow_receiver.closed_by_peer(timeout=5.0))


if __name__ == "__main__":
    unittest.main()
