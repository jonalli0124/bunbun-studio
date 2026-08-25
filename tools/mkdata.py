import os, json, base64
from io import BytesIO
from PIL import Image
from repo_paths import species_dir, OBJECTS, ROOMS as ROOMDIR, TOOLS
C=species_dir()
FUR={(0xbe,0x73,0x30),(0x80,0x4d,0x36)}; TUN={(0x91,0x51,0xd3),(0x5d,0x22,0x9d)}
def anchors(im, generic=False):
    # generic=True is for OTHER ANIMALS (Jon 8/18: "same animations but different
    # animals"): the tunic purples are the style-lock and shared across every pack, but
    # fur colours differ per species - so a hand is any solid non-tunic, non-outline
    # pixel flanking the tunic, instead of the capybara's exact browns.
    px=im.load(); bb=im.getchannel('A').point(lambda v:255 if v>127 else 0).getbbox()
    if not bb: return None
    def is_fur(r,g,b):
        if generic: return (r,g,b) not in TUN and 90 < r+g+b < 700
        return (r,g,b) in FUR
    x0,y0,x1,y1=bb; cap=y0+int((y1-y0)*0.80); L=[];R=[]
    for y in range(y0,min(y1,cap)):
        purp=[x for x in range(x0,x1) if px[x,y][3]>127 and px[x,y][:3] in TUN]
        if not purp: continue
        pl,pr=min(purp),max(purp)
        for x in range(x0,x1):
            r,g,b,al=px[x,y]
            if al>127 and is_fur(r,g,b):
                if x<pl: L.append((x,y))
                elif x>pr: R.append((x,y))
    def tip(p):
        if not p: return None
        ym=max(q[1] for q in p); t=[q for q in p if q[1]>=ym-2]
        return [round(sum(q[0] for q in t)/len(t)), round(sum(q[1] for q in t)/len(t))]
    Lp,Rp=tip(L),tip(R)
    return {"L":Lp,"R":Rp,
            "mid":([ (Lp[0]+Rp[0])//2,(Lp[1]+Rp[1])//2 ] if Lp and Rp else None),
            "head":[ (x0+x1)//2, y0 ],
            "bbox":[x0,y0,x1,y1]}
def b64(p,trim=False):
    im=Image.open(p).convert('RGBA')
    if trim:
        bb=im.getchannel('A').point(lambda v:255 if v>127 else 0).getbbox()
        if bb: im=im.crop(bb)
    b=BytesIO(); im.save(b,'PNG')
    return "data:image/png;base64,"+base64.b64encode(b.getvalue()).decode(), im.size

def gather_clips(root, generic=False, label=""):
    clips={}
    for c in sorted(os.listdir(root)):
        d=os.path.join(root,c)
        if not os.path.isdir(d): continue
        # The diagonals stay out - six near-identical entries would drown the clip picker. But EAST
        # and WEST are how travel is actually shipped (generate east, mirror west), and Adult_Walk
        # exists ONLY in directional form, so skipping all of them dropped the walk entirely.
        # ...except the BABY CRAWL, which ships as diagonals ONLY (the 8/25 crawl ruling:
        # the generator's angle bias makes profile crawls unreliable, E/W borrow the
        # diagonals) - skip those and the baby has no travel animation at all.
        if c.endswith(('_NorthWest','_NorthEast','_SouthWest','_SouthEast')) \
           and not c.startswith('Baby_Crawl_'): continue
        # A CLIP FOLDER IS A PHASE FOLDER, and nothing else is (Jon 2026-08-23: "what is the
        # croc head and tails? that shouldnt be there ... as a separate secion to select in the
        # animation creation"). This walked EVERY directory under the pack, so the croc's
        # heads/ and tails/ - 64 loose part sheets left over from its generation session -
        # arrived in the clip picker as if they were animations a child could pose. Named
        # rather than silently dropped: a folder in a pack that is not a clip is either
        # working material that belongs elsewhere, or a clip that is misnamed, and both are
        # worth seeing at build time.
        if not c.startswith(('Adult_', 'Baby_')):
            print(f"   not a clip folder, skipped: {label}{c}")
            continue
        # Baby_ clips are IN again (Jon 2026-08-25: the age concept — "you select animal
        # and age so we can do different rooms based on their age"; supersedes the 8/18
        # adult-only ruling). The assembler groups clips by their Adult_/Baby_ prefix;
        # teen is a UI placeholder over the adult set until teen art exists.
        fr=[]
        for f in sorted(x for x in os.listdir(d) if x.endswith('.png')):
            im=Image.open(os.path.join(d,f)).convert('RGBA')
            a=anchors(im, generic); uri,_=b64(os.path.join(d,f))
            if a: a["img"]=uri; a["name"]=f; fr.append(a)
        if not fr: continue
        ys=[x["mid"][1] for x in fr if x["mid"]]
        # A STATE is generated as eight compass views, not as a sequence. Cycling one is a
        # turntable, so the tools have to be able to tell the two apart.
        DIRS=("east","north-east","north","north-west","west","south-west","south","south-east")
        names=[os.path.splitext(x["name"])[0] for x in fr]
        rotations=bool(names) and all(n in DIRS for n in names)
        clips_face_on=0
        if rotations:
            for x in fr: x["dir"]=os.path.splitext(x["name"])[0]
            # WHICH ONE FACES THE CAMERA. The labels are not consistent between states - Adult_Sit's
            # "south" is a three-quarter view while Adult_Idle's is face on - so it is measured
            # rather than assumed: the camera-facing pose is the symmetric one, so mirror each view
            # and keep whichever differs least from itself.
            best, bestScore, cand = 0, None, []
            for idx, x in enumerate(fr):
                im = Image.open(os.path.join(d, x["name"])).convert("RGBA")
                bb = im.getchannel("A").point(lambda v: 255 if v > 127 else 0).getbbox()
                if not bb:
                    continue
                crop = im.crop(bb)
                mir = crop.transpose(Image.FLIP_LEFT_RIGHT)
                a, b = crop.load(), mir.load()
                w, h = crop.size
                diff = 0
                for yy in range(0, h, 2):
                    for xx in range(0, w, 2):
                        pa, pb = a[xx, yy], b[xx, yy]
                        if (pa[3] > 127) != (pb[3] > 127):
                            diff += 3           # a silhouette mismatch counts most
                        elif pa[3] > 127:
                            diff += (abs(pa[0]-pb[0]) + abs(pa[1]-pb[1]) + abs(pa[2]-pb[2])) / 255.0
                score = diff / max(1, (w // 2) * (h // 2))
                # Symmetry alone picks the BACK just as happily as the front - both are symmetric,
                # and it chose north.png, which is the back of his head. What separates them is the
                # face: this character's dot eyes and snout are near-black pixels high in the
                # sprite, and the back view has none.
                eyes = 0
                for yy in range(0, max(1, int(h * 0.55))):
                    for xx in range(w):
                        px = a[xx, yy]
                        if px[3] > 127 and (px[0] + px[1] + px[2]) < 190:
                            eyes += 1
                cand.append((idx, score, eyes))
            # Two rules, in order. The compass names vary in how they are ANGLED between states, but
            # a camera-facing view is always some flavour of south - north is his back in every set
            # here. Within the southern three, the one showing the most face is the face-on one;
            # a sleeping capybara has its eyes shut, so symmetry breaks the tie when no view has
            # much of a face to show.
            south = [cc for cc in cand if 'south' in fr[cc[0]]['dir']]
            pool = south or cand
            most_eyes = max(cc[2] for cc in pool)
            if most_eyes > 0:
                pool = [cc for cc in pool if cc[2] >= most_eyes * 0.8] or pool
            best = min(pool, key=lambda cc: cc[1])[0]
            clips_face_on = best
        clips[c]={"frames":fr,"rest":(max(ys) if ys else None),"rotations":rotations}
        if rotations:
            clips[c]["faceOn"]=fr[clips_face_on]["dir"]
            print("   %s%-14s faces the camera in %s" % (label, c, fr[clips_face_on]["dir"]))
    return clips

clips=gather_clips(C)

# OTHER ANIMALS (Jon 8/18: "we will have the same animations but different animals").
# Any ANIMS-layout tree under assets/characters/<id> (Adult_* folders) joins the editor's
# animal picker. Pack-layout trees (capybara-pilot style) are skipped - they become ANIMS
# via mkspecies first.
characters={}
CHARROOT=os.path.join(os.path.dirname(ROOMDIR),'characters')
if os.path.isdir(CHARROOT):
    for cid in sorted(os.listdir(CHARROOT)):
        d=os.path.join(CHARROOT,cid)
        if not os.path.isdir(d): continue
        if not any(x.startswith('Adult_') for x in os.listdir(d)): continue
        cc=gather_clips(d, generic=True, label=cid+'/')
        if cc:
            characters[cid]={"clips":cc}
            print(f"   animal '{cid}': {len(cc)} clips")

# Two kinds of object, and they behave differently: a MARK is an overlay composited above
# the head to say how the pet feels; a PROP is a physical thing that can also stand in the room.
props={}
for kind in ('marks','props','lights'):
    folder=os.path.join(OBJECTS,kind)
    if not os.path.isdir(folder): continue
    for f in sorted(x for x in os.listdir(folder) if x.endswith('.png')):
        n=os.path.splitext(f)[0]
        if n in props: continue
        uri,size=b64(os.path.join(folder,f),trim=True)
        props[n]={"img":uri,"w":size[0],"h":size[1],"kind":kind[:-1]}   # 'mark' | 'prop'
# rooms give the editor a real backdrop, so a mark can be checked against the colours it will
# actually sit on - the white-outline `angry` mark disappears on a pale room
ROOMS=ROOMDIR
rooms={}
if os.path.isdir(ROOMS):
    for f in sorted(x for x in os.listdir(ROOMS) if x.endswith('.png') and not x.startswith('_')):
        im=Image.open(os.path.join(ROOMS,f)).convert('RGBA')
        if im.size!=(320,240): continue
        uri,_=b64(os.path.join(ROOMS,f))
        rooms[os.path.splitext(f)[0]]=uri

out=os.path.join(TOOLS,'build','attach_data.json')
json.dump({"clips":clips,"characters":characters,"props":props,"rooms":rooms,
           "scr":{"w":320,"h":240,"floor":200}},open(out,'w'))
print(f"{len(clips)} clips, {len(characters)} extra animals, {len(props)} props, {len(rooms)} rooms -> {round(os.path.getsize(out)/1024)} KB")
print("clips: " + ", ".join(clips))
