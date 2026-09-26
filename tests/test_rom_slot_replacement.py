"""Disposable SQLite/SAV fault-injection tests; no real ROMs or user saves."""
import base64
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from integral_emulator.api import LeagueApplication
from integral_emulator.errors import NotFoundError, ValidationError
from integral_emulator.sessions import LinkSessionStatus


class ReplacementTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.app = LeagueApplication(Path(self.temp.name))
        self.app.allow_self_registration = True
        self.users = []
        for name in ('replace_a', 'replace_b'):
            self.app.handle_request('POST', '/auth/register', {'username': name, 'password': 'test-password-123'})
            login = self.app.handle_request('POST', '/auth/login', {'username': name, 'password': 'test-password-123'})
            token = login['token']['token']
            user = self.app.require_user(token)
            self.users.append((user.id, token))
        self.first = self.apply(self.item('a'))['slots'][0]

    def item(self, letter, slot=1):
        return dict(
            slot=slot,
            filename=f'{letter}.gbc',
            sha256=letter*64,
            platform='gb',
            region='JP',
            rom_header_title=f'TEST {letter.upper()}',
            generated_initial_save_data=base64.b64encode(
                bytes([0xFF]) * (32 * 1024)
            ).decode('ascii'),
        )

    def apply(self, *items, confirm=False):
        current = {
            slot.slot: slot
            for slot in self.app.rom_slots.list_for_user(self.users[0][0])
        }
        desired_items = []
        for item in items:
            desired = dict(item)
            existing = current.get(int(desired['slot']))
            if (
                existing is not None
                and existing.sha256 == desired.get('sha256')
                and existing.filename == desired.get('filename')
            ):
                desired.pop('generated_initial_save_data', None)
            desired_items.append(desired)
        return self.app.handle_request('POST', '/rom-slots/apply',
            {'slots': desired_items, 'confirm_delete_saves': confirm}, self.users[0][1])

    def pair(self):
        uid, token = self.users[1]
        second = self.app.handle_request('POST', '/rom-slots/apply',
                    {'slots': [self.item('a')]}, token)['slots'][0]
        session = self.app.sessions.create_session(
            self.users[0][0], uid, self.first['save_id'], second['save_id'],
            lock_expires_at=self.app.game_session_lease_expires_at(), link_mode='trade',
            auth_session_ids={user: hashlib.sha256(tok.encode()).hexdigest() for user, tok in self.users})
        self.app.sessions.transition(session.id, LinkSessionStatus.PREPARING)
        self.app.sessions.transition(session.id, LinkSessionStatus.RUNNING)
        self.app.sessions.transition(session.id, LinkSessionStatus.FINALIZING)
        directory = self.app.sessions.session_temp_path(session)
        self.app.storage.atomic_write_bytes(directory/'player_a.sav', b'new-a')
        self.app.storage.atomic_write_bytes(directory/'player_b.sav', b'new-b')
        return session, second

    def interrupt_pair(self, after_first):
        session, second = self.pair()
        original = self.app.saves.commit_locked
        calls = 0
        def stop(*args, **kwargs):
            nonlocal calls
            calls += 1
            if (after_first and calls == 2) or not after_first:
                raise OSError('injected process stop')
            return original(*args, **kwargs)
        with patch.object(self.app.saves, 'commit_locked', side_effect=stop):
            with self.assertRaises(OSError):
                self.app.sessions.commit_changed_staged_saves(session.id)
        self.app.sessions.transition(session.id, LinkSessionStatus.RECOVERING)
        return session, second

    def test_confirmation_and_successful_retry(self):
        preview = self.apply(self.item('b'))
        self.assertTrue(preview['requires_confirmation'])
        self.assertEqual(self.app.rom_slots.list_for_user(self.users[0][0])[0].save_id, self.first['save_id'])
        result = self.apply(self.item('b'), confirm=True)
        again = self.apply(self.item('b'), confirm=True)
        self.assertEqual(result['slots'][0], again['slots'][0])
        with self.assertRaises(NotFoundError):
            self.app.saves.get_save(self.first['save_id'])

    def test_multi_slot_failure_rolls_back_all_metadata(self):
        original = self.app.storage.update_rom_slot
        def fail(*args, **kwargs):
            original(*args, **kwargs)
            raise OSError('injected after slot write')
        old = self.app.saves.download(self.first['save_id'], self.users[0][0])
        with patch.object(self.app.storage, 'update_rom_slot', side_effect=fail):
            with self.assertRaises(OSError):
                self.apply(self.item('b'), self.item('c', 2), confirm=True)
        self.assertEqual(self.app.rom_slots.list_for_user(self.users[0][0])[0].save_id, self.first['save_id'])
        self.assertEqual(self.app.saves.download(self.first['save_id'], self.users[0][0]), old)
        self.assertEqual(len(self.app.saves.list_for_user(self.users[0][0])), 1)

    def test_terminal_history_does_not_block(self):
        session, second = self.pair()
        self.app.sessions.transition(session.id, LinkSessionStatus.CANCELLED)
        self.apply(self.item('b'), confirm=True)
        self.assertIsNotNone(self.app.saves.get_save(second['save_id']))
        with self.assertRaises(ValidationError):
            self.app.sessions.commit_changed_staged_saves(session.id)

    def test_unapplied_pair_is_cancelled_without_changing_peer(self):
        session, second = self.interrupt_pair(False)
        before = self.app.saves.download(second['save_id'], self.users[1][0])
        self.apply(self.item('b'), confirm=True)
        self.assertEqual(self.app.saves.download(second['save_id'], self.users[1][0]), before)
        self.assertEqual(self.app.sessions.get_session(session.id).status, 'CANCELLED')

    def test_partial_pair_finishes_peer_before_replacement(self):
        session, second = self.interrupt_pair(True)
        self.apply(self.item('b'), confirm=True)
        self.assertEqual(self.app.saves.download(second['save_id'], self.users[1][0])[1], b'new-b')
        self.assertEqual(self.app.sessions.get_session(session.id).status, 'COMPLETED')
        self.assertEqual(self.app.storage.get_pair_save_journal(session.id)['state'], 'COMMITTED')
        # The partner can subsequently replace their own save, too.
        self.app.handle_request('POST', '/rom-slots/apply',
            {'slots': [self.item('c')], 'confirm_delete_saves': True}, self.users[1][1])

    def test_recovery_can_retry_after_another_atomic_write_failure(self):
        session, second = self.interrupt_pair(True)
        def fail(point):
            if point == 'after_atomic_replace':
                raise OSError('recovery interrupted')
        with patch.object(self.app.saves.commit_service, 'fault_injector', side_effect=fail):
            with self.assertRaises(OSError):
                self.apply(self.item('b'), confirm=True)
        self.assertEqual(self.app.rom_slots.list_for_user(self.users[0][0])[0].save_id, self.first['save_id'])
        self.apply(self.item('b'), confirm=True)
        self.assertEqual(self.app.saves.download(second['save_id'], self.users[1][0])[1], b'new-b')

    def test_changed_peer_revision_is_not_overwritten(self):
        session, second = self.interrupt_pair(True)
        with self.app.authority_database.transaction(write=True) as c:
            c.execute('UPDATE save_records SET revision=revision+1 WHERE save_id=?', (second['save_id'],))
        with self.assertRaisesRegex(ValidationError, 'revision/hash conflict'):
            self.apply(self.item('b'), confirm=True)
        self.assertEqual(self.app.saves.download(second['save_id'], self.users[1][0])[1], bytes([0xFF]) * 32768)

    def test_missing_partial_candidate_blocks_without_slot_change(self):
        session, second = self.interrupt_pair(True)
        (self.app.sessions.session_temp_path(session)/'player_b.sav').unlink()
        with self.assertRaisesRegex(ValidationError, 'candidate missing'):
            self.apply(self.item('b'), confirm=True)
        self.assertEqual(self.app.rom_slots.list_for_user(self.users[0][0])[0].save_id, self.first['save_id'])
        self.assertEqual(self.app.saves.download(second['save_id'], self.users[1][0])[1], bytes([0xFF]) * 32768)
        self.assertIsNone(self.app.sessions.active_game_session_for_user(self.users[1][0]))

    def test_active_game_refuses_replacement(self):
        self.pair()
        with self.assertRaisesRegex(ValidationError, 'active game session'):
            self.apply(self.item('b'), confirm=True)

    def test_cleanup_failure_is_not_registration_failure(self):
        with patch.object(self.app.saves, 'remove_save_files', side_effect=OSError('injected cleanup')):
            with self.assertLogs('integral_emulator.rom_slots', level='ERROR'):
                result = self.apply(self.item('b'), confirm=True)
        self.assertEqual(result['cleanup_pending_save_ids'], [self.first['save_id']])
        self.assertEqual(result['slots'][0]['filename'], 'b.gbc')

    def test_both_written_before_journal_completion(self):
        session, second = self.pair()
        original = self.app.storage.update_pair_save_journal
        def fail(record, version):
            if record['state'] == 'COMMITTED':
                raise OSError('injected before pair completion')
            return original(record, version)
        with patch.object(self.app.storage, 'update_pair_save_journal', side_effect=fail):
            with self.assertRaises(OSError):
                self.app.sessions.commit_changed_staged_saves(session.id)
        self.app.sessions.release_locks(session)
        self.app.sessions.release_game_session_locks(session)
        before = self.app.saves.get_save(second['save_id'])
        self.apply(self.item('b'), confirm=True)
        self.assertEqual(before, self.app.saves.get_save(second['save_id']))
        self.assertEqual(self.app.sessions.get_session(session.id).status, 'COMPLETED')

    def test_atomic_file_replaced_before_metadata(self):
        session, second = self.pair()
        def fail(point):
            if point == 'after_atomic_replace':
                raise OSError('injected after atomic replace')
        with patch.object(self.app.saves.commit_service, 'fault_injector', side_effect=fail):
            with self.assertRaises(OSError):
                self.app.sessions.commit_changed_staged_saves(session.id)
        self.app.sessions.release_locks(session)
        self.app.sessions.release_game_session_locks(session)
        self.apply(self.item('b'), confirm=True)
        self.assertEqual(self.app.saves.download(second['save_id'], self.users[1][0])[1], b'new-b')

    def test_mobile_history_is_detached_but_peer_save_is_untouched(self):
        uid, token = self.users[0]
        auth = hashlib.sha256(token.encode()).hexdigest()
        with self.app.authority_database.transaction(write=True) as c:
            c.execute("INSERT INTO game_runs (game_run_id,server_id,execution_mode,status,auth_session_id,created_at_ms,expires_at_ms) VALUES ('old-mobile','primary','MOBILE_CLIENT','COMPLETED',?,1,2)", (auth,))
            c.execute("INSERT INTO mobile_sessions (session_id,server_id,user_id,save_id,rom_id,auth_session_id,package_id,release_id,package_digest,game_run_id,fencing_token,scenario_id,status,created_at_ms,updated_at_ms,lease_expires_at_ms) VALUES ('old-mobile','primary',?,?,?,?, 'pkg','release',?,'old-mobile',1,'default','COMPLETED',1,2,2)",
                      (uid, self.first['save_id'], self.first['rom_id'], auth, 'a'*64))
            c.execute("INSERT INTO mobile_create_requests (request_digest,user_id,request_id,save_id,rom_id,rom_header_title,package_id,release_id,package_digest,scenario_id,reserved_session_id,mobile_session_id,state,created_at_ms,updated_at_ms,auth_session_id) VALUES (?,?, 'mobile-create:test',?,?,'TEST A','pkg','release',?,'default','old-mobile','old-mobile','CREATED',1,2,?)",
                      ('b'*64, uid, self.first['save_id'], self.first['rom_id'], 'a'*64, auth))
        self.assertTrue(self.apply(self.item('b'))['requires_confirmation'])
        result = self.apply(self.item('b'), confirm=True)
        with self.app.authority_database.transaction() as c:
            self.assertIsNone(c.execute("SELECT save_id FROM mobile_sessions WHERE session_id='old-mobile'").fetchone()[0])
            self.assertEqual(c.execute('SELECT count(*) FROM mobile_create_requests').fetchone()[0], 0)
        self.assertEqual(len(self.app.saves.list_for_user(uid)), 1)
        self.assertNotEqual(result['slots'][0]['save_id'], self.first['save_id'])
        self.assertTrue(self.app.authority_database.integrity_report()['ok'])


if __name__ == '__main__':
    unittest.main()
