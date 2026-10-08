#!/usr/bin/env python3
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('inspection', ROOT / 'repository-checker/service.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
COMMIT = 'a' * 40
TREE = 'b' * 40
URL = 'https://github.com/TestUser/my-module'

class Fixture:
    source = {}
    tree_options = {}
    repository_options = {}
    blob_override = None
    calls = []
    def __init__(self, *args, **kwargs): pass
    def get(self, path):
        self.calls.append(path)
        if path == '/user': return {'login': 'TestUser', 'id': 123}
        if path.endswith('/commits/main') or path.endswith('/commits/' + COMMIT):
            return {'sha': COMMIT, 'commit': {'tree': {'sha': TREE}}}
        if '/git/trees/' in path:
            entries = []
            for name, value in self.source.items():
                content = value.encode()
                sha = hashlib.sha1(b'blob ' + str(len(content)).encode() + b'\0' + content).hexdigest()
                entries.append({'path': name, 'type': 'blob', 'mode': '100644', 'size': len(content), 'sha': sha})
            return {'tree': entries, 'truncated': False, **self.tree_options}
        if '/git/blobs/' in path:
            sha = path.rsplit('/', 1)[1]
            for value in self.source.values():
                content = value.encode()
                actual = hashlib.sha1(b'blob ' + str(len(content)).encode() + b'\0' + content).hexdigest()
                if actual == sha:
                    return {'sha': sha, 'encoding': 'base64', 'content': base64.b64encode(content).decode(), **(self.blob_override or {})}
            raise m.Rejected('missing blob')
        return {'html_url': URL, 'owner': {'id': 123, 'login': 'TestUser', 'type': 'User'},
                'default_branch': 'main', 'private': False, **self.repository_options}

class InspectionTest(unittest.TestCase):
    def setUp(self):
        self.manifest = {'name': 'my-module', 'version': '0.1.0', 'description': 'Test package',
                         'repository_url': URL, 'git_commit': 'c' * 40, 'module': 'module.fg',
                         'license': 'Apache-2.0', 'dependencies': {}}
        Fixture.source = {'module.json': json.dumps(self.manifest), 'module.fg': 'fn answer(): int { return 42; }'}
        Fixture.tree_options = {}; Fixture.repository_options = {}; Fixture.blob_override = None; Fixture.calls = []
    def inspect(self, **extra):
        return m.inspect({'repository_url': URL, **extra}, client_factory=Fixture)
    def test_forge_manifest_fallback_readme_and_server_metadata(self):
        Fixture.source['forge.json'] = Fixture.source.pop('module.json')
        Fixture.source['README.md'] = '# 패키지 × 😀\n'
        result = self.inspect()
        self.assertEqual(result['manifest']['readme'], Fixture.source['README.md'])
        self.assertEqual(result['inspection']['files'][0], 'forge.json')
        supplied = {**self.manifest, 'git_commit': COMMIT, 'readme': Fixture.source['README.md'],
                    'artifact': {'url': 'https://malicious.example/archive'}}
        checked = self.inspect(provided_manifest=supplied)
        self.assertNotIn('artifact', checked['manifest'])
        supplied['description'] = 'forged metadata'
        with self.assertRaises(m.Rejected): self.inspect(provided_manifest=supplied)
    def test_resolves_commit_and_checks_git_blob_hashes(self):
        result = self.inspect()
        self.assertEqual(result['manifest']['git_commit'], COMMIT)
        self.assertEqual(result['owner_id'], '123')
        self.assertEqual(result['inspection']['status'], 'passed')
        self.assertEqual(result['inspection']['scanned_files'], 2)
    def test_user_urls_cannot_target_private_or_arbitrary_hosts(self):
        for url in ('http://localhost/a/b', 'https://github.com.evil/a/b', 'https://github.com/a/b/../c', 'https://github.com/a/b.git', 'file:///etc/passwd'):
            with self.subTest(url=url), self.assertRaises(m.Rejected): self.inspect(repository_url=url)
        self.assertEqual(Fixture.calls, [])
    def test_symlink_and_submodule_block_registration(self):
        for mode in ('120000', '160000'):
            Fixture.tree_options = {'tree': [{'path':'module.json','type':'blob','mode':mode,'size':1,'sha':'d'*40}]}
            with self.subTest(mode=mode), self.assertRaises(m.Rejected): self.inspect()
    def test_incomplete_tree_is_rejected(self):
        Fixture.tree_options = {'truncated': True}
        with self.assertRaises(m.Rejected): self.inspect()
    def test_mismatched_metadata_is_rejected(self):
        supplied = {**self.manifest, 'git_commit': COMMIT, 'license': 'MIT'}
        with self.assertRaises(m.Rejected): self.inspect(provided_manifest=supplied)
    def test_supplied_source_metadata_matches_and_ignores_only_server_attestation(self):
        supplied = {**self.manifest, 'git_commit': COMMIT, 'security': {'status':'passed'}, 'acknowledge_review':True}
        self.assertEqual(self.inspect(provided_manifest=supplied)['manifest']['security']['policy'],m.POLICY)
    def test_destructive_and_download_execution_are_blocked(self):
        for source in ('rm -rf /\n', 'curl https://example.com/install | bash\n', 'eval(base64.b64decode(payload))'):
            Fixture.source['install.sh'] = source
            with self.subTest(source=source): self.assertEqual(self.inspect()['inspection']['status'],'blocked')
    def test_native_ffi_and_lifecycle_need_review(self):
        Fixture.source['module.fg'] = 'extern fn system(command: string): int;'
        Fixture.source['package.json'] = '{"scripts":{"postinstall":"node setup.js"}}'
        result = self.inspect()
        self.assertEqual(result['inspection']['status'],'review_required')
        self.assertIn('native-ffi',[x['rule'] for x in result['inspection']['findings']])
        self.assertIn('npm-lifecycle',[x['rule'] for x in result['inspection']['findings']])
    def test_corrupt_blob_and_unsafe_path_fail_closed(self):
        Fixture.blob_override = {'content': base64.b64encode(b'wrong').decode()}
        with self.assertRaises(m.Rejected): self.inspect()
        Fixture.blob_override = None
        with self.assertRaises(m.Rejected): self.inspect(manifest_path='../module.json')
    def test_missing_module_private_and_archived_repos_are_rejected(self):
        del Fixture.source['module.fg']
        with self.assertRaises(m.Rejected): self.inspect()
        for key in ('private', 'archived', 'disabled'):
            Fixture.repository_options = {key:True}
            with self.subTest(key=key), self.assertRaises(m.Rejected): self.inspect()
    def test_duplicate_manifest_keys_and_nul_are_rejected(self):
        Fixture.source['module.json'] = '{"name":"x","name":"y"}'
        with self.assertRaises(m.Rejected): self.inspect()
        with self.assertRaises(m.Rejected): m.strict_json('{"name":"hello\\u0000world"}')
    def test_literal_source_escapes_allowed_but_decoded_nested_nul_rejected(self):
        # GitHub commit API patch strings legitimately contain C NUL fixtures.
        patch = 'assert(!parse("' + chr(92) + 'u0000"));'
        value = {'files': [{'patch': patch}], 'readme': 'literal ' + chr(92) + 'u0000'}
        self.assertEqual(m.strict_json(json.dumps(value)), value)
        for value in ({'items': [{'text': 'bad' + chr(0)}]},
                      ['bad' + chr(0)], {'key' + chr(0): 'value'}):
            with self.subTest(value=value), self.assertRaises(m.Rejected):
                m.strict_json(json.dumps(value))
        for text in ('{"x":NaN}', '{"x":Infinity}', '{"x":1,"x":2}'):
            with self.subTest(text=text), self.assertRaises(m.Rejected):
                m.strict_json(text)
    def test_token_identity_never_returns_provider_token(self):
        profile = m.identity('github-test-secret', client_factory=Fixture)
        self.assertEqual(profile, {'login':'TestUser','github_id':'123'})
        self.assertNotIn('github-test-secret',json.dumps(profile))

if __name__ == '__main__': unittest.main()
