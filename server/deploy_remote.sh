#!/usr/bin/env bash
set -e

export HOST="192.168.178.2"
export TARGET_DIR="/home/tears/Server-CachyOS-EliteDesk"
export SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[1/3] Copying files to remote host ${HOST}..."
python3 -c "
import pty, os, select

host = os.environ['HOST']
target_dir = os.environ['TARGET_DIR']
src_dir = os.environ['SRC_DIR']

def run_ssh(cmd, password):
    pid, fd = pty.fork()
    if pid == 0:
        os.execvp(cmd[0], cmd)
    else:
        output = b''
        while True:
            r, _, _ = select.select([fd], [], [], 15)
            if not r: break
            try: data = os.read(fd, 1024)
            except OSError: break
            if not data: break
            output += data
            if b'password:' in output.lower():
                os.write(fd, password.encode() + b'\n')
                output = b''
            elif b'are you sure you want to continue' in output.lower():
                os.write(fd, b'yes\n')
                output = b''
        os.waitpid(pid, 0)
        return output.decode('utf-8', errors='ignore')

pw = 'root#!701Kirara'
for f in ['fast_pixconv.c', 'server.py', 'run_server.sh', 'install_server.sh', 'README.md']:
    print(f'Uploading {f}...')
    run_ssh(['scp', '-o', 'StrictHostKeyChecking=no', f'{src_dir}/{f}', f'root@{host}:/tmp/{f}'], pw)

print('Executing remote update commands...')
cmds = f'''
set -e
cp /tmp/fast_pixconv.c {target_dir}/fast_pixconv.c
cp /tmp/server.py {target_dir}/server.py
cp /tmp/run_server.sh {target_dir}/run_server.sh
cp /tmp/install_server.sh {target_dir}/install_server.sh
cp /tmp/README.md {target_dir}/README.md
chown -R tears:tears {target_dir}
chmod +x {target_dir}/server.py {target_dir}/run_server.sh {target_dir}/install_server.sh
cd {target_dir}
gcc -O3 -march=native -mtune=native -fopenmp -DHAVE_TURBOJPEG_LZ4 -shared -fPIC fast_pixconv.c -lturbojpeg -llz4 -o fast_pixconv.so
chown tears:tears fast_pixconv.so
systemctl restart vortexbrowser.service
systemctl status vortexbrowser.service --no-pager
'''
print(run_ssh(['ssh', '-o', 'StrictHostKeyChecking=no', f'root@{host}', cmds], pw))
"
