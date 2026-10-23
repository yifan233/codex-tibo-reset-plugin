import hashlib, importlib.util, io, json, tempfile, unittest, zipfile
from pathlib import Path
script=Path(__file__).resolve().parents[1] / 'tibo-reset/scripts/ensure_app.py'
spec=importlib.util.spec_from_file_location('ensure_app',script);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class BootstrapTests(unittest.TestCase):
 def archive(self,name='TiboReset.exe',body=b'MZdemo'):
  b=io.BytesIO()
  with zipfile.ZipFile(b,'w') as z:z.writestr(name,body)
  return b.getvalue()
 def reader(self,data,checksum=True,key="Windows-x86_64"):
  name=m.ASSETS[key]
  url='https://github.com/yifan233/tibo-rest-plugin/releases/download/v0.1.0/'+name
  asset={'name':name,'browser_download_url':url}
  if checksum:asset['digest']='sha256:'+hashlib.sha256(data).hexdigest()
  release={'tag_name':'v0.1.0','assets':[asset]}
  def get(u):return json.dumps(release).encode() if u.endswith('/releases/latest') else data
  return get
 def test_reuse_no_download(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);(root/'TiboReset.exe').write_bytes(b'MZ')
   result=m.ensure_app(root,'Windows-x86_64',lambda _:self.fail('Unexpected network'))
   self.assertEqual(result['status'],'found')
 def test_mac_download_and_reuse(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)/'app'
   name='Reset Countdown.app/Contents/MacOS/ResetCountdown'
   result=m.ensure_app(root,'Darwin-arm64',self.reader(self.archive(name),key='Darwin-arm64'))
   self.assertEqual(result['status'],'installed')
   self.assertEqual(Path(result['executable']).read_bytes(),b'MZdemo')
   self.assertEqual(Path(result['executable']).stat().st_mode & 0o777,0o755)
   self.assertEqual(m.ensure_app(root,'Darwin-arm64',lambda _:self.fail('Unexpected network'))['status'],'found')
 def test_download_and_preserve_state(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);config=root/'countdown.state';config.write_bytes(b'existing state')
   result=m.ensure_app(root,'Windows-x86_64',self.reader(self.archive()))
   self.assertEqual(result['status'],'installed');self.assertEqual(config.read_bytes(),b'existing state')
 def test_first_install_has_no_configuration_file(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)/'new';m.ensure_app(root,'Windows-x86_64',self.reader(self.archive()))
   self.assertEqual([p.name for p in root.iterdir()],['TiboReset.exe'])
 def test_bad_hash_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   good=self.reader(self.archive());data=self.archive(body=b'MZchanged')
   def reader(u):return good(u) if u.endswith('/releases/latest') else data
   with self.assertRaises(ValueError):m.ensure_app(Path(d),'Windows-x86_64',reader)
   self.assertFalse((Path(d)/'TiboReset.exe').exists())
 def test_missing_checksum_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(ValueError):m.ensure_app(Path(d),'Windows-x86_64',self.reader(self.archive(),False))
 def test_path_traversal_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(ValueError):m.ensure_app(Path(d),'Windows-x86_64',self.reader(self.archive('../escaped')))
 def test_unsupported_platform(self):
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(ValueError):m.ensure_app(Path(d),'Linux-x86_64',lambda _:self.fail('Unexpected network'))
 def test_platform_alias(self):
  self.assertEqual(m.platform_key('Windows','AMD64'),'Windows-x86_64')
  self.assertEqual(m.platform_key('Darwin','aarch64'),'Darwin-arm64')
 def test_redirect_rejected(self):
  for url in ['http://github.com/a', 'https://evil.test/a', 'https://github.com@evil.test/a']:
   with self.assertRaises(ValueError):
    m.ReleaseRedirect().redirect_request(None,None,302,'',{},url)
 def test_safe_url(self):
  for url in ['https://api.github.com/repos/a/b/releases/latest', 'https://release-assets.githubusercontent.com/a']:
   m.validate_url(url)
 def test_archive_unsafe_paths(self):
  for name in ['/absolute', 'C:/bad', 'a\\b', 'CON', 'nul.txt', 'a./b', './bad']:
   with self.subTest(name=name), tempfile.TemporaryDirectory() as d:
    with self.assertRaises(ValueError):m.safe_extract(self.archive(name),Path(d))
 def test_case_duplicate_rejected(self):
  data=io.BytesIO()
  with zipfile.ZipFile(data,'w') as z:
   z.writestr('TiboReset.exe',b'a');z.writestr('tiboreset.EXE',b'b')
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(ValueError):m.safe_extract(data.getvalue(),Path(d))
 def test_symlink_archive_rejected(self):
  data=io.BytesIO()
  with zipfile.ZipFile(data,'w') as z:
   info=zipfile.ZipInfo('TiboReset.exe');info.create_system=3;info.external_attr=0o120777 << 16
   z.writestr(info,'outside')
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(ValueError):m.safe_extract(data.getvalue(),Path(d))
 def test_local_symlink_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);outside=root/'outside';outside.write_bytes(b'unchanged')
   (root/'TiboReset.exe').symlink_to(outside)
   with self.assertRaises(ValueError):m.ensure_app(root,'Windows-x86_64',lambda _:self.fail('Unexpected network'))
   self.assertEqual(outside.read_bytes(),b'unchanged')
 def test_excess_entries_rejected(self):
  data=io.BytesIO()
  with zipfile.ZipFile(data,'w') as z:
   for i in range(1025):z.writestr(str(i),b'')
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(ValueError):m.safe_extract(data.getvalue(),Path(d))

if __name__ == '__main__':
 unittest.main()
