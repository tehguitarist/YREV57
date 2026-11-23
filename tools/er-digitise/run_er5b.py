import numpy as np, csv
from PIL import Image, ImageDraw
im0=Image.open('/home/claude/rev5_om/er/p-23.png').convert('L')
im=np.array(im0)<128
blocks={'S-HALL':(810,1047),'L-HALL':(1499,1735),'RANDOM':(2186,2423),'REVERSE':(2874,3112)}
PXDB=4.4; TOPOFF=215; pxms=1.505; xl=1410
rows=[]
rgb=im0.convert('RGB'); d=ImageDraw.Draw(rgb)
for name,(byL,byR) in blocks.items():
    for ch,by0 in (('L',byL),('R',byR)):
        # refine baseline: strongest row within +-8
        by=max(range(by0-8,by0+9),key=lambda y:im[y,xl:xl+900].sum())
        h=[]
        xs=range(xl+3,int(xl+605*pxms))
        for x in xs:
            y=by-3;run=0;gap=0
            while y>by-260:
                if im[y,x]: run+=1+gap;gap=0
                else:
                    gap+=1
                    if gap>14:break
                y-=1
            h.append(run)
        h=np.array(h); cols=np.where(h>=8)[0]
        groups=[]
        for c in cols:
            if groups and c-groups[-1][-1]<=1: groups[-1].append(c)
            else: groups.append([c])
        for i,g in enumerate(groups):
            xs_=np.array(g)+xl+3; hh=h[g]
            xc=(xs_*hh).sum()/hh.sum(); top=by-3-hh.max()
            gain=-(top-(by-TOPOFF))/PXDB; t=(xc-xl)/pxms
            if t<3 or t>598: continue
            rows.append((name,4.0,ch,i+1,round(t,1),round(gain,1),len(g)))
            if (name,ch) in (('S-HALL','L'),('RANDOM','R'),('REVERSE','L')):
                X=xc; Y=(by-TOPOFF)-gain*PXDB
                d.ellipse((X-6,Y-6,X+6,Y+6),outline=(255,0,0),width=2)
rgb.crop((1330,520,2420,900)).save('ov5_shall.png')
rgb.crop((1330,2250,2420,2560)).save('ov5_random.png')
rgb.crop((1330,2590,2420,2900)).save('ov5_reverse.png')
import collections
print(collections.Counter((r[0],r[2]) for r in rows))
with open('rev5_er_taps_size4_raw.csv','w',newline='') as f:
    w=csv.writer(f); w.writerow(['mode','room_size','channel','tap','time_ms','gain_db','pixel_width']); w.writerows(rows)
