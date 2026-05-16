import unittest

from .liteim_e2e import E2ETestCase, MessageType, TlvType, unique_name


class AuthE2ETest(E2ETestCase):
    def test_register_login_and_list_friends(self):
        username = unique_name("auth")
        password = "secret"

        with self.connect() as client:
            register = client.register(username, password, "Auth User")
            self.assertEqual(register.msg_type, MessageType.REGISTER_RESPONSE)
            user_id = register.uint64(TlvType.USER_ID)
            self.assertGreater(user_id, 0)

            login = client.login(username, password)
            self.assertEqual(login.msg_type, MessageType.LOGIN_RESPONSE)
            self.assertEqual(login.uint64(TlvType.USER_ID), user_id)
            self.assertGreater(login.uint64(TlvType.SESSION_ID), 0)

            friends = client.list_friends()
            self.assertEqual(friends.msg_type, MessageType.LIST_FRIENDS_RESPONSE)

    def test_wrong_password_and_login_limiter_return_errors(self):
        username = unique_name("limited")
        password = "secret"

        with self.connect() as client:
            client.register(username, password, "Limited User")

            for _ in range(3):
                error = client.login(username, "wrong-password", expected=MessageType.ERROR_RESPONSE)
                self.assertIn("invalid username or password", error.string(TlvType.ERROR_MESSAGE))

            limited = client.login(username, "wrong-password", expected=MessageType.ERROR_RESPONSE)
            self.assertIn("too many login failures", limited.string(TlvType.ERROR_MESSAGE))


if __name__ == "__main__":
    unittest.main()
