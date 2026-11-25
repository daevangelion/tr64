# SPDX-License-Identifier: MIT
"""Exact field comparisons after shortest-edit sequence alignment; no repair."""
from collections import Counter
import alignment
from binary import FormatError


def gameplay(level):
    rooms = [dict(info=r.info,z_sectors=r.z_sectors,x_sectors=r.x_sectors,
        sectors=r.sectors,portals=r.portals,
        static_instances=[{k:v for k,v in s.items() if k!='reserved'} for s in r.static_instances],
        alternate_room=r.alternate_room,flags=r.flags) for r in level.rooms]
    result = dict(rooms=rooms)
    for key in ('floor_data','items','boxes','overlaps','zones','animations','states',
                'ranges','commands','bones','frames','cameras','sound_sources',
                'static_meshes','models','cinematic_frames'):
        value = level.data[key]
        if key=='models':
            value = [{k:v for k,v in row.items() if k!='reserved'} for row in value]
        result[key] = value
    return result


def trigger_references(level,start):
    """TR1 floor action layout, TRosettaStone3 sections 4.4/4.6.

    Switch/key/pickup first action is an item index; actions 0/6 reference items.
    Camera's extra word supplies the action-list end bit. Source words stay raw.
    """
    words = level.data['floor_data']
    result = []
    i = start
    if not start:
        return result
    while True:
        if i>=len(words):
            raise FormatError(f'{level.name}: floor function runs beyond word table')
        header = words[i]
        i += 1
        function = header&31
        if function in (1,2,3):
            i += 1
        elif function==4:
            i += 1  # trigger setup
            first = True
            while True:
                if i>=len(words):
                    raise FormatError(f'{level.name}: truncated floor trigger action')
                offset,command = i,words[i]
                i += 1
                action,target = (command>>10)&31,command&1023
                if (first and ((header>>8)&127) in (2,3,4)) or action in (0,6):
                    result.append(dict(action='activator' if first and ((header>>8)&127) in (2,3,4) else action,
                                       word_offset=offset,item=target))
                first = False
                end = command&0x8000
                if action==1:
                    if i>=len(words):
                        raise FormatError(f'{level.name}: truncated floor camera parameters')
                    end = words[i]&0x8000
                    i += 1
                if end:
                    break
        elif function not in (0,5):
            raise FormatError(f'{level.name}: unsupported TR1 floor function {function}')
        if header&0x8000:
            return result


def renumbering(pc,psx,item_map):
    starts = set()
    added,removed = [],[]
    for room_index in range(max(len(pc.rooms),len(psx.rooms))):
        aa = pc.rooms[room_index].sectors if room_index<len(pc.rooms) else []
        bb = psx.rooms[room_index].sectors if room_index<len(psx.rooms) else []
        for sector_index in range(max(len(aa),len(bb))):
            fa = aa[sector_index]['floor_index'] if sector_index<len(aa) else 0
            fb = bb[sector_index]['floor_index'] if sector_index<len(bb) else 0
            if fa and fb:
                starts.add((fa,fb))
            elif fa or fb:
                level,start,target = (pc,fa,removed) if fa else (psx,fb,added)
                for ref in trigger_references(level,start):
                    item = ref['item']
                    target.append(dict(room=room_index,sector=sector_index,floor_index=start,
                        **ref,item_type=level.data['items'][item]['type'] if item<len(level.data['items']) else None,
                        mapped_psx_item=item_map.get(item) if fa else None))
    checked,changed,unmapped,mismatches = 0,0,0,[]
    pc_total,psx_total = len(removed),len(added)
    derived_floor_words = set()
    for sa,sb in sorted(starts):
        a,b = trigger_references(pc,sa),trigger_references(psx,sb)
        pc_total += len(a)
        psx_total += len(b)
        if [r['action'] for r in a]!=[r['action'] for r in b]:
            mismatches.append(dict(pc_floor=sa,psx_floor=sb,reason='different item-action sequence',pc=a,psx=b))
            continue
        for ra,rb in zip(a,b):
            expected = item_map.get(ra['item'])
            if expected is None:
                unmapped += 1
                continue
            checked += 1
            if expected!=rb['item']:
                mismatches.append(dict(pc=ra,psx=rb,expected_psx_item=expected))
            elif ra['item']!=rb['item']:
                changed += 1
                if (pc.data['floor_data'][ra['word_offset']]&0xfc00)==(psx.data['floor_data'][rb['word_offset']]&0xfc00):
                    derived_floor_words.add((ra['word_offset'],rb['word_offset']))
    no_refs = not (pc_total or psx_total)
    consistent = None if no_refs else not (mismatches or unmapped or added or removed)
    status = ('no item references' if no_refs else 'added/removed trigger references' if added or removed
              else 'consistent renumbering' if consistent else 'unresolved/inconsistent references')
    return dict(aligned_items=len(item_map),item_index_map={str(k):v for k,v in item_map.items() if k!=v},
        checked_trigger_references=checked,consistently_renumbered_references=changed,
        pc_total_item_references=pc_total,psx_total_item_references=psx_total,
        added_item_references=added,removed_item_references=removed,
        added_reference_count=len(added),removed_reference_count=len(removed),
        unmapped_references=unmapped,inconsistent_references=len(mismatches),
        consistent=consistent,status=status,examples=mismatches[:5],
        renumbering_consistent=(not mismatches and not unmapped) if checked else None,
        derived_floor_word_pairs=[dict(pc_word=a,psx_word=b) for a,b in sorted(derived_floor_words)],
        reference_rule='TRosettaStone3 4.6 plus pickup activator; actions 0/6; camera extra word',
        counting_rule='paired distinct floor-start pairs plus every one-sided room/sector reference')


def compare(pc,psx):
    left,right = gameplay(pc),gameplay(psx)
    classes,edits = {},{}
    maps = {name:alignment.index_map(left[name],right[name]) for name in ('ranges','floor_data','items','overlaps')}
    maps['ranges'][len(left['ranges'])] = len(right['ranges'])
    # Match states after applying PROVEN range-index shifts only to comparison
    # keys. The raw state records are still walked and their changed offsets
    # reported as derived findings; the IR is never normalised.
    state_keys = [dict(row,range_offset=maps['ranges'].get(row['range_offset'],row['range_offset']))
                  for row in left['states']]
    state_codes = alignment.opcodes(state_keys,right['states'])
    maps['states'] = {i+offset:k+offset for tag,i,j,k,l in state_codes if tag=='equal' for offset in range(j-i)}
    maps['states'][len(left['states'])] = len(right['states'])
    derived_fields = {'state_offset':'states','range_offset':'ranges','floor_index':'floor_data'}
    changed_items = set()
    refs = renumbering(pc,psx,maps['items'])
    derived_floor_words = {(row['pc_word'],row['psx_word']) for row in refs['derived_floor_word_pairs']}
    derived_pc_words = {a for a,b in derived_floor_words}
    derived_psx_words = {b for a,b in derived_floor_words}

    def record(path,field,a,b,kind,derived=None):
        # Primary and proven renumbering findings are distinct classes.
        name = field+('.derived_renumbering' if derived else '')
        entry = classes.setdefault(name,dict(kind=kind,differences=0,numeric_deltas=Counter(),examples=[],
            causal_class='derived' if derived else 'primary',derived_from=derived))
        entry['differences'] += 1
        if not derived and kind=='value' and field.startswith('level.items[].'):
            changed_items.add(path.rsplit('.',1)[0])
        if isinstance(a,int) and isinstance(b,int):
            entry['numeric_deltas'][b-a] += 1
        if len(entry['examples'])<5:
            entry['examples'].append(dict(path=path,pc=a,psx=b))

    def walk(a,b,path,field):
        if isinstance(a,dict) and isinstance(b,dict):
            for key in sorted(a.keys()|b.keys()):
                av,bv = a.get(key),b.get(key)
                table = derived_fields.get(key)
                if av!=bv and key=='overlap' and field=='level.boxes[]' and (av&0x8000)==(bv&0x8000) and maps['overlaps'].get(av&0x7fff)==(bv&0x7fff):
                    record(path+'.'+key,field+'.'+key,av,bv,'value','overlaps edit script, index mask 0x7fff')
                elif av!=bv and table and maps[table].get(av)==bv:
                    record(path+'.'+key,field+'.'+key,av,bv,'value',table+' edit script')
                else:
                    walk(av,bv,path+'.'+key,field+'.'+key)
        elif isinstance(a,(tuple,list)) and isinstance(b,(tuple,list)):
            # Rooms/sectors are coordinate-addressed grids, not reorderable
            # object sequences. Preserve those positional identities.
            codes = [('replace',0,len(a),0,len(b))] if field in ('level.rooms','level.rooms[].sectors') else alignment.opcodes(a,b)
            if field=='level.states':
                codes = state_codes
            sections = []
            for tag,i,j,k,l in codes:
                if tag=='equal':
                    if field=='level.states':
                        for offset in range(j-i):
                            if a[i+offset]!=b[k+offset]:
                                walk(a[i+offset],b[k+offset],f'{path}[pc:{i+offset},psx:{k+offset}]',field+'[]')
                    continue
                sections.append(dict(operation=tag,pc_start=i,pc_count=j-i,psx_start=k,psx_count=l-k,
                    pc_examples=a[i:min(j,i+5)],psx_examples=b[k:min(l,k+5)]))
                paired = min(j-i,l-k) if tag=='replace' else 0
                for offset in range(paired):
                    pair_path = f'{path}[pc:{i+offset},psx:{k+offset}]'
                    if field=='level.floor_data' and (
                        (i+offset,k+offset) in derived_floor_words or
                        (i+offset in derived_pc_words and k+offset in derived_psx_words and
                         (a[i+offset]&0xfc00)==(b[k+offset]&0xfc00) and
                         maps['items'].get(a[i+offset]&0x03ff)==(b[k+offset]&0x03ff))):
                        record(pair_path,field+'[]',a[i+offset],b[k+offset],'value',
                               'items edit script, trigger argument mask 0x03ff')
                    else:
                        walk(a[i+offset],b[k+offset],pair_path,field+'[]')
                for offset in range(i+paired,j):
                    record(f'{path}[pc:{offset}]',field+'[].deleted',a[offset],None,'deletion')
                for offset in range(k+paired,l):
                    record(f'{path}[psx:{offset}]',field+'[].inserted',None,b[offset],'insertion')
            if sections and field not in ('level.rooms','level.rooms[].sectors'):
                edits[path] = dict(pc_count=len(a),psx_count=len(b),hunks=sections,
                    touched_pc=sum(h['pc_count'] for h in sections),touched_psx=sum(h['psx_count'] for h in sections))
                if field=='level.states':
                    edits[path]['alignment_basis'] = 'range offsets mapped for matching; raw offset changes reported separately'
        elif a!=b:
            record(path,field,a,b,'value')

    walk(left,right,'level','level')
    for entry in classes.values():
        delta = entry['numeric_deltas']
        entry['classification'] = ('constant psx-minus-pc offset among mismatches' if entry['differences']>1 and len(delta)==1
            and sum(delta.values())==entry['differences'] else 'single observation' if entry['differences']==1 else 'nonconstant or structural')
        entry['numeric_deltas'] = {str(k):v for k,v in sorted(delta.items())}
    lighting = Counter()
    coordinates_match = 0
    for a,b in zip(pc.rooms,psx.rooms):
        for va,vb in zip(a.vertices,b.vertices):
            if tuple(va[k] for k in ('x','y','z'))==tuple(vb[k] for k in ('x','y','z')):
                coordinates_match += 1
                lighting[va['lighting']['value']-(8191-(vb['lighting']['value']<<5))] += 1
    primary = {name:c for name,c in classes.items() if c['causal_class']=='primary'}
    primary_count = sum(c['differences'] for c in primary.values())
    derived_count = sum(c['differences'] for c in classes.values() if c['causal_class']=='derived')
    floor = edits.get('level.floor_data')
    overview = dict(items_inserted=primary.get('level.items[].inserted',{}).get('differences',0),
        items_deleted=primary.get('level.items[].deleted',{}).get('differences',0),items_changed=len(changed_items),
        added_trigger_references=refs['added_reference_count'],removed_trigger_references=refs['removed_reference_count'],
        trigger_status=refs['status'],floor_edit_hunks=len(floor['hunks']) if floor else 0,
        floor_words_pc=floor['touched_pc'] if floor else 0,floor_words_psx=floor['touched_psx'] if floor else 0,
        room_flag_changes=primary.get('level.rooms[].flags',{}).get('differences',0),
        box_field_changes=sum(c['differences'] for name,c in primary.items() if name.startswith('level.boxes')),
        other_primary_classes={name:c['differences'] for name,c in primary.items() if
            not name.startswith(('level.items','level.floor_data','level.rooms[].flags','level.boxes'))})
    return dict(level=pc.name,pc_sha256=pc.sha256,psx_sha256=psx.sha256,equal=not classes,
        difference_count=sum(c['differences'] for c in classes.values()),classes=classes,
        primary_difference_count=primary_count,derived_difference_count=derived_count,primary_gameplay_summary=overview,
        sequence_edits=edits,item_reference_renumbering=refs,
        lighting_evidence=dict(pc_encoding='tr1_darkness_13bit',psx_encoding='psx_vertex_8bit',
            matched_coordinates=coordinates_match,formula='pc_raw - (8191 - (psx_raw << 5))',
            residual_histogram={str(k):v for k,v in sorted(lighting.items())}))


def markdown(report):
    lines = ['# M0 PC / PlayStation gameplay equivalence','','Shortest insert/delete edit scripts align sequences before field comparisons.',
        'Rooms and their sector grids retain positional identities. Insertions/deletions',
        'are primary findings, not thousands of downstream field changes. Raw words are',
        'never rewritten. Proven index shifts are labelled derived, not suppressed.','',
        f'Unpaired PC: {report["unpaired_pc"]}; unpaired PSX: {report["unpaired_psx"]}.','',
        'Name mapping: case-insensitive filename stem identity.','']
    lines += ['## Primary gameplay overview', '',
        'Derived index renumberings are excluded from the primary totals below.',
        'Floor column lists raw edit hunks (words are unchanged in the IR); semantic',
        'item-trigger additions/removals and renumbering are reported separately.',
        'Item ~ counts changed aligned records, not changed fields. Detailed sections',
        'retain every raw edit, primary/derived class and first examples.', '',
        '| Level | Primary / derived findings | Items + / − / ~ | Trigger refs + / −; status | Floor raw hunks; PC/PSX words | Room flags | Box primary fields | Other primary classes |',
        '|---|---:|---|---|---|---:|---:|---|']
    for row in report['levels']:
        s = row['primary_gameplay_summary']
        other = '; '.join(f'{name.removeprefix("level.")}: {count}' for name,count in s['other_primary_classes'].items()) or 'none'
        lines.append(f'| {row["level"]} | {row["primary_difference_count"]} / {row["derived_difference_count"]} | '
            f'{s["items_inserted"]} / {s["items_deleted"]} / {s["items_changed"]} | '
            f'{s["added_trigger_references"]} / {s["removed_trigger_references"]}; {s["trigger_status"]} | '
            f'{s["floor_edit_hunks"]}; {s["floor_words_pc"]}/{s["floor_words_psx"]} | '
            f'{s["room_flag_changes"]} | {s["box_field_changes"]} | {other} |')
    lines += ['', 'One-sided trigger reference details (room/sector, source item/type):', '']
    for row in report['levels']:
        refs = row['item_reference_renumbering']
        for label,key in [('Added in PSX','added_item_references'),('Removed from PC','removed_item_references')]:
            for ref in refs[key]:
                lines.append(f'- {row["level"]}: {label}, room {ref["room"]} sector {ref["sector"]}, '
                    f'item {ref["item"]} (type {ref["item_type"]}), action {ref["action"]}, floor word {ref["word_offset"]}.')
    lines += ['', '## Full evidence by level', '']
    for row in report['levels']:
        lines += [f'## {row["level"]}','',f'PC `{row["pc_sha256"]}`; PSX `{row["psx_sha256"]}`.','',
            f'{row["difference_count"]} field/insert/delete findings: {row["primary_difference_count"]} primary, '
            f'{row["derived_difference_count"]} derived; {len(row["classes"])} classes.','']
        for path,edits in row['sequence_edits'].items():
            lines += [f'### Edit script `{path}`','',
                f'Counts {edits["pc_count"]} -> {edits["psx_count"]}; {len(edits["hunks"])} hunks, '
                f'{edits["touched_pc"]} PC / {edits["touched_psx"]} PSX touched elements.','']
            for h in edits['hunks']:
                lines.append(f'- {h["operation"]}: PC [{h["pc_start"]}, +{h["pc_count"]}], PSX [{h["psx_start"]}, +{h["psx_count"]}]; PC {h["pc_examples"]}; PSX {h["psx_examples"]}')
            lines.append('')
        for field,entry in sorted(row['classes'].items()):
            lines += [f'### `{field}`','',f'{entry["differences"]} {entry["kind"]}; {entry["causal_class"]}; '
                f'{entry["classification"]}; derived from {entry["derived_from"]}.',
                f'Delta histogram `{entry["numeric_deltas"]}`.','']
            for ex in entry['examples']:
                lines.append(f'- `{ex["path"]}`: PC `{ex["pc"]}`; PSX `{ex["psx"]}`')
            lines.append('')
        lines += ['Item-index / floor-trigger renumbering evidence:',str(row['item_reference_renumbering']),'',
                  'Lighting encoding residual evidence:',str(row['lighting_evidence']),'']
    return '\n'.join(lines)+'\n'
