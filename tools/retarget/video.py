# Видео стенда для проверки клипов (docs/anim.md): оба скелета рядом (слева game_engine, справа Mixamo), очередь
# клипов, три стороны в один ряд, подписи. Нужны Xvfb, lavapipe, ffmpeg; игра — build/linux-release.
#   python3 tools/retarget/video.py <выход.mp4> [--clips a,b,…] [--speed 100|50|25] [--close плечи|колени]
#                                   [--size 640x720] [--work папка]
# Петля играет два круга, остальные клипы — раз и 0.4 с стоят (как очередь seq= в src/debug/stand.cpp)
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from animlib import read_labels  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
GAME = os.path.join(ROOT, 'build', 'linux-release', 'anathemyre')
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
# Стенд: место первого человека и середина между двумя (src/debug/stand.cpp: STAND_X/Z, STAND_GAP, лицо на юго-запад)
MID = (11698.0 + 0.389, -2348.5 - 0.389)
# Стороны: камера (yaw) смотрит на лицо, на правый бок, на спину три четверти
VIEWS = [('спереди', 330), ('справа', 225), ('сзади', 150)]
# Крупно (--close): один человек на скелет, две панели рядом; наклон, даль, опора над землёй (м), сторона
CLOSE = {'плечи': (6, 1.5, 1.35, 280), 'колени': (15, 2.4, 0.9, 280)}
ONE = (11698.0, -2348.5)


def schedule(clips, speed):
    out, t = [], 0.0
    for c in clips:
        lab = read_labels(os.path.join(ROOT, 'data', 'anims', c + '.txt'))
        keys = int(next(w[1] for w in lab if w[0] == 'keys'))
        loop = any(w[0] == 'loop' for w in lab)
        dur = (keys - 1) / 30.0
        play = 2 * dur if loop else dur + 0.4
        out.append((c, t, t + play / speed))
        t += play / speed
    return out, t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--clips', default='walk,run,turn180,stairs,sword_a,sword_b,lunge,block,dodge,fall,getup')
    ap.add_argument('--speed', type=int, default=100)
    ap.add_argument('--close', default='')
    ap.add_argument('--size', default='640x720')
    ap.add_argument('--views', default='')
    ap.add_argument('--work', default='/tmp/anathemyre-video')
    a = ap.parse_args()
    clips = a.clips.split(',')
    sched, total = schedule(clips, a.speed / 100.0)
    lead = 20  # кадров выдержки до первого клипа: глаз привыкает к свету
    frames = int(total * 30) + 1 + lead
    w, h = a.size.split('x')
    os.makedirs(a.work, exist_ok=True)
    # Панели: подпись, камера, стенд
    if a.close:
        pitch, dist, above, yaw = CLOSE[a.close]
        cam = ['--cam', str(yaw), str(pitch), str(dist), str(ONE[0]), str(ONE[1]), '--pivot', str(above)]
        panels = [(f'game_engine, {a.close}', cam, ''), (f'Mixamo, {a.close}', cam, 'mixamo,')]
    else:
        panels = [(n, ['--cam', str(y), '8', '4.6', str(MID[0]), str(MID[1])], 'both,') for n, y in VIEWS
                  if not a.views or n in a.views.split(',')]
    parts = []
    for i, (name, cam, extra) in enumerate(panels):
        d = os.path.join(a.work, f'p{i}')
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        cmd = ['xvfb-run', '-a', '-s', '-screen 0 1600x900x24', GAME, '--nohud', '--frames', str(frames),
               '--window', w, h, *cam, '--hour', '15.5', '--stand',
               f"{extra}seq={'+'.join(clips)},speed={a.speed},t=-{lead * 100 // 30}", '--video', os.path.join(d, 'f')]
        print(' '.join(cmd), flush=True)
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if r.returncode:
            print(r.stdout[-3000:])
            sys.exit(1)
        lab = os.path.join(d, 'v.mp4')
        txt = [f"drawtext=fontfile={FONT}:text='{name}':x=12:y=10:fontsize=22:fontcolor=white:borderw=2"]
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-framerate', '30', '-start_number', str(lead + 1),
                        '-i', os.path.join(d, 'f_%04d.bmp'),
                        '-vf', ','.join(txt), '-pix_fmt', 'yuv420p', '-c:v', 'libx264', '-crf', '20', lab], check=True)
        parts.append(lab)
    # Ряд сторон и подписи клипов по времени
    inputs = sum((['-i', p] for p in parts), [])
    stack = f"hstack=inputs={len(parts)}" if len(parts) > 1 else 'null'
    sp = '' if a.speed == 100 else f' (×{a.speed / 100:g})'
    who = '' if a.close else '  —  слева game_engine, справа Mixamo'
    tx = [f"drawtext=fontfile={FONT}:text='{c}{sp}{who}':x=(w-tw)/2:y=h-40:"
          f"fontsize=26:fontcolor=white:borderw=2:enable='between(t,{t0:.3f},{t1:.3f})'" for c, t0, t1 in sched]
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', *inputs, '-filter_complex', f"{stack},{','.join(tx)}",
                    '-pix_fmt', 'yuv420p', '-c:v', 'libx264', '-crf', '22', a.out], check=True)
    print(f'video: {a.out}, {total:.1f} с, клипов {len(clips)}')


if __name__ == '__main__':
    main()
