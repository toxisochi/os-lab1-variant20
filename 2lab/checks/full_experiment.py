import itertools
import json
import math
import os
from pathlib import Path
import random
import statistics
import subprocess
import time

root = Path(__file__).resolve().parents[1]
program = str(root / '2lab')
out = root / 'checks' / 'full_results'
out.mkdir(exist_ok=True)
rng = random.Random(20)


def encode(points):
    return str(len(points)) + '\n' + '\n'.join(' '.join(map(str, p)) for p in points) + '\n'


def run(data, args):
    p = subprocess.run([program] + args, input=data, text=True,
                       capture_output=True, check=True)
    return dict(line.split(': ', 1) for line in p.stdout.splitlines())


cases = [[(0,0,0), (3,0,0), (0,4,0)], [(i, 2*i, -i) for i in range(10)],
         [(0,0,0)] * 6, [(0,0,0),(1,0,0),(1,1,0),(0,1,0)]]
for _ in range(25):
    cases.append([tuple(rng.randint(-20,20) for _ in range(3))
                  for _ in range(rng.randint(3,15))])
for points in cases:
    areas = []
    for a,b,c in itertools.combinations(points,3):
        x,y,z = math.dist(a,b), math.dist(a,c), math.dist(b,c)
        s = (x+y+z)/2
        if math.isclose(2*max(x,y,z), x+y+z, abs_tol=1e-12, rel_tol=1e-14):
            areas.append(0.0)
        else:
            areas.append(math.sqrt(max(0,s*(s-x)*(s-y)*(s-z))))
    expected = max(areas)
    baseline = run(encode(points), ['-s'])
    for p in (1,2,3,4,32):
        result = run(encode(points), ['-t',str(p)])
        assert result['Indices'] == baseline['Indices']
        assert math.isclose(float(result['Area']),expected,abs_tol=2e-6,rel_tol=1e-7), (points,result,expected)
        assert int(result['Threads']) <= p
bad = ['2\n0 0 0\n1 1 1\n', '3\n0 0 0\n',
       '3\n0 0 0\n1 0 0\n0 nan 0\n', '3\n0 0 0\n1 0 0\n0 inf 0\n',
       '3\n0 0 0\n1 0 0\n0 1 0\nextra', '3\n0 0 0\n1 0 0\n0 1e100 0\n']
for data in bad:
    assert subprocess.run([program,'-t','2'],input=data,text=True,capture_output=True).returncode
for args in ([], ['-t','0'], ['-t','-1'], ['-t','abc'], ['-t','3abc'],
             ['-t','999999999999999999999999999']):
    assert subprocess.run([program]+args,input='',text=True,capture_output=True).returncode
print('Correctness checks passed',flush=True)

cores = os.cpu_count()
counts = [1,2,4,8,cores,64,128,1024]
assert len(set(counts)) == 8 and 8 < cores < 64
rows = []
for n in (1100,1500,2000):
    points = [tuple(rng.randint(-1000,1000) for _ in range(3)) for _ in range(n)]
    data = encode(points)
    (out / f'points_{n}.txt').write_text(data)
    baseline = run(data,['-s'])
    baseline_indices = baseline['Indices']
    for p in [0]+counts:
        args = ['-s'] if p == 0 else ['-t',str(p)]
        samples = []
        for repeat in range(3):
            result = run(data,args)
            assert result['Indices'] == baseline_indices
            assert result['Area'] == baseline['Area']
            assert int(result['Threads']) == max(1,p)
            samples.append(float(result['Time_ms']))
        median = statistics.median(samples)
        if p == 0:
            t_seq = median
        rows.append(dict(n=n,threads=p,ms=median,samples_ms=samples,
                         speedup=t_seq/median,efficiency=t_seq/median/max(1,p)))
        print(n,p,round(median,3),flush=True)

data = (out / 'points_2000.txt').read_text()
process = subprocess.Popen([program,'-t','12'],stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
process.stdin.write(data)
process.stdin.close()
snapshot = ''
for _ in range(100):
    result = subprocess.run(['ps','-L','-p',str(process.pid),'-o','pid,lwp,nlwp,comm'],
                            text=True,capture_output=True)
    if len(result.stdout.strip().splitlines()) == 13:
        snapshot = result.stdout
        break
    if process.poll() is not None:
        break
    time.sleep(0.01)
process.stdout.read()
assert process.wait() == 0 and snapshot
(out / 'ps_threads.txt').write_text(snapshot)
metadata = {
    'date':'2026-11-16', 'logical_cores':cores,
    'cpu':subprocess.check_output(['bash','-lc',"lscpu | sed -n 's/^Model name: *//p'"],text=True).strip(),
    'os':subprocess.check_output(['uname','-sr'],text=True).strip(),
    'gcc':subprocess.check_output(['g++','--version'],text=True).splitlines()[0],
    'checks':'29 datasets x sequential and 5 thread limits; 12 invalid input cases; ps -L: 12 threads',
    'rows':rows,
}
(out / 'results.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2))
print('DONE',flush=True)
