import unittest

from .liteim_e2e import E2ETestCase, MessageType, TlvType, unique_name


class GroupChatE2ETest(E2ETestCase):
    def test_three_users_can_group_chat_and_read_group_history(self):
        owner_name = unique_name("owner")
        member_name = unique_name("member")
        third_name = unique_name("third")

        with self.connect() as owner, self.connect() as member, self.connect() as third:
            owner_id = owner.register_and_login(owner_name, "secret")
            member_id = member.register_and_login(member_name, "secret")
            third_id = third.register_and_login(third_name, "secret")

            group_id = owner.create_group("python e2e group")
            member.join_group(group_id)
            third.join_group(group_id)

            response = owner.group_message(group_id, "hello group from python e2e")
            self.assertEqual(response.msg_type, MessageType.GROUP_MESSAGE_RESPONSE)
            self.assertEqual(response.uint64(TlvType.SENDER_ID), owner_id)
            self.assertEqual(response.uint64(TlvType.RECEIVER_ID), group_id)

            member_push = member.expect_push(MessageType.GROUP_MESSAGE_PUSH)
            third_push = third.expect_push(MessageType.GROUP_MESSAGE_PUSH)
            self.assertEqual(member_push.uint64(TlvType.SENDER_ID), owner_id)
            self.assertEqual(third_push.uint64(TlvType.SENDER_ID), owner_id)
            self.assertEqual(member_push.string(TlvType.MESSAGE_TEXT), "hello group from python e2e")
            self.assertEqual(third_push.string(TlvType.MESSAGE_TEXT), "hello group from python e2e")

            history = member.history_group(group_id, limit=10)
            records = history.message_records()
            self.assertIn(owner_id, [record.sender_id for record in records])
            self.assertGreater(member_id, 0)
            self.assertGreater(third_id, 0)
            self.assertIn("hello group from python e2e", [record.text for record in records])


if __name__ == "__main__":
    unittest.main()
