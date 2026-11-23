import numpy as np, csv, sys
from PIL import Image
sys.path.insert(0,'.')
def load(p): return np.array(Image.open(p).convert('L'))<128
pages={'p-23.png':{'modes':[1,2,3],'L':[1009,1942,2883],'R':[1371,2308,3233]},
       'p-24.png':{'modes':[4,5,6],'L':[1004,1944,2869],'R':[1366,2301,3222]}}
names={1:'SMALL HALL',2:'LARGE HALL',3:'RANDOM',4:'REVERSE',5:'PLATE',6:'SPRING'}
PXDB=6.2; TOP=307.5
XL={(1,1.0):257.5,(1,4.0):1371,(2,1.0):272,(2,4.0):1373,(3,1.0):276,(3,4.0):1375,(4,1.0):295.5,(4,4.0):1394.5,(5,1.0):306,(5,4.0):1398,(6,1.0):309,(6,4.0):1403}
def baseline_extent(im,by0,xg):
    best=None
    for by in range(by0-12,by0+13):
        row=im[by-1:by+2,:].any(axis=0)
        # longest stretch with gaps<=4 within window
        xs=np.where(row[xg-120:xg+1000])[0]+xg-120
        if len(xs)==0: continue
        groups=[[xs[0],xs[0]]]
        for x in xs[1:]:
            if x-groups[-1][1]<=4: groups[-1][1]=x
            else: groups.append([x,x])
        g=max(groups,key=lambda g:g[1]-g[0])
        L=g[1]-g[0]
        if best is None or L>best[0]: best=(L,by,g[0],g[1])
    return best
rows=[];cal=[]
for pg,d in pages.items():
    im=load('/home/claude/rev7_work/er/'+pg)
    for k,m in enumerate(d['modes']):
        for size,xg in ((1.0,265),(4.0,1385)):
            for ch in 'LR':
                L,by,xl,xr=baseline_extent(im,d[ch][k],xg)
                pxms=1.612; xl=XL[(m,size)]
                pass
                # heights
                xl=int(round(xl)); xs=range(xl+3,int(xl+520*pxms))
                h=[]
                for x in xs:
                    y=by-3;run=0;gap=0
                    while y>by-330:
                        if im[y,x]: run+=1+gap;gap=0
                        else:
                            gap+=1
                            if gap>2:break
                        y-=1
                    h.append(run)
                h=np.array(h)
                cols=np.where(h>=8)[0]
                groups=[]
                for c in cols:
                    if groups and c-groups[-1][-1]<=1: groups[-1].append(c)
                    else: groups.append([c])
                for i,g in enumerate(groups):
                    xs_=np.array(g)+xl+3
                    hh=h[g]
                    xc=(xs_*hh).sum()/hh.sum()
                    top=by-3-hh.max()
                    gain=-(top-(by-TOP))/PXDB
                    rows.append((m,names[m],size,ch,i+1,round((xc-xl)/pxms,1),round(gain,1),len(g)))

with open('rev7_er_taps_raw.csv','w',newline='') as f:
    w=csv.writer(f); w.writerow(['mode','mode_name','room_size','channel','tap','time_ms','gain_db','pixel_width']); w.writerows(rows)
import collections
cnt=collections.Counter((r[0],r[2],r[3]) for r in rows); print(sorted(cnt.items()))
