pi import os, urllib.request as u, tempfile as t; f='/usr/share/gef/gef.py'; c=os.path.join(t.gettempdir(), 'gef-cached.py'); p=f if os.path.exists(f) else (c if os.path.exists(c) and os.path.getsize(c)>0 else None); (open(c, 'wb+').write(u.urlopen('https://tinyurl.com/gef-main').read()), os.chmod(c, 0o600), gdb.execute('source %s' % c)) if not p else gdb.execute('source %s' % p)

