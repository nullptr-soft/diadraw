// Generates assets/builtin/bpmn: BPMN 2.0 shapes drawn for DiaDraw (original artwork).
// node tools/gen/bpmn_library.mjs, then node tools/gen/builtin_libraries.mjs
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../assets/builtin/bpmn/', import.meta.url));
rmSync(join(root, 'shapes'), { recursive: true, force: true });
mkdirSync(join(root, 'shapes'), { recursive: true });

const INK = '#1f2937';
const PAPER = '#ffffff';
const shapes = {};
const svg = (w, h, body) =>
  `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${w} ${h}">${body}</svg>\n`;
const add = (id, entry, w, h, body) => {
  writeFileSync(join(root, 'shapes', `${id}.svg`), svg(w, h, body));
  shapes[id] = { ...entry, svg: `shapes/${id}.svg`, width: w, height: h };
};
const words = (id) => id.replace(/_/g, ' ').replace(/^\w/, (c) => c.toUpperCase());

// ------------------------------------------------------------------ events

/** Event markers centred on (20, 20); `filled` for throwing events. */
const MARKERS = {
  message: (f) =>
    `<rect x="11.5" y="14" width="17" height="12" fill="${f ? INK : PAPER}" stroke="${INK}" stroke-width="1.3"/><path d="M11.5 14 L20 20.5 L28.5 14" fill="none" stroke="${f ? PAPER : INK}" stroke-width="1.3"/>`,
  timer: () =>
    `<circle cx="20" cy="20" r="9.5" fill="${PAPER}" stroke="${INK}" stroke-width="1.3"/>` +
    [0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330]
      .map((a) => {
        const r = (a * Math.PI) / 180;
        const p = (d) =>
          `${(20 + Math.sin(r) * d).toFixed(2)} ${(20 - Math.cos(r) * d).toFixed(2)}`;
        return `<path d="M${p(7.5)} L${p(9.5)}" stroke="${INK}" stroke-width="1"/>`;
      })
      .join('') +
    `<path d="M20 20 L20 13.5 M20 20 L24.5 22" stroke="${INK}" stroke-width="1.3" fill="none"/>`,
  error: (f) =>
    `<path d="M12.5 28 L16.5 14.5 L22 22 L27.5 11.5 L24.5 26 L18.5 18.5 Z" fill="${f ? INK : PAPER}" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/>`,
  escalation: (f) =>
    `<path d="M20 11 L27 28 L20 21.5 L13 28 Z" fill="${f ? INK : PAPER}" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/>`,
  cancel: (f) =>
    `<path d="M13 15.5 L15.5 13 L20 17.5 L24.5 13 L27 15.5 L22.5 20 L27 24.5 L24.5 27 L20 22.5 L15.5 27 L13 24.5 L17.5 20 Z" fill="${f ? INK : PAPER}" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/>`,
  compensation: (f) =>
    `<path d="M11 20 L19.5 14.5 V25.5 Z M19.5 20 L28 14.5 V25.5 Z" fill="${f ? INK : PAPER}" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/>`,
  conditional: () =>
    `<rect x="13.5" y="12" width="13" height="16" fill="${PAPER}" stroke="${INK}" stroke-width="1.3"/><path d="M15.5 15.5 H24.5 M15.5 19 H24.5 M15.5 22.5 H24.5 M15.5 26 H24.5" stroke="${INK}" stroke-width="1"/>`,
  link: (f) =>
    `<path d="M11.5 17 H21 V13 L29 20 L21 27 V23 H11.5 Z" fill="${f ? INK : PAPER}" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/>`,
  signal: (f) =>
    `<path d="M20 11 L29 27 H11 Z" fill="${f ? INK : PAPER}" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/>`,
  terminate: () => `<circle cx="20" cy="20" r="10" fill="${INK}"/>`,
  multiple: (f) =>
    `<path d="M20 11 L28.6 17.2 L25.3 27.3 H14.7 L11.4 17.2 Z" fill="${f ? INK : PAPER}" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/>`,
  parallel_multiple: () =>
    `<path d="M17.5 11 H22.5 V17.5 H29 V22.5 H22.5 V29 H17.5 V22.5 H11 V17.5 H17.5 Z" fill="${PAPER}" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/>`,
};

/** Which markers each event position allows (BPMN 2.0, table 10.89 and friends). */
const POSITIONS = {
  start: {
    label: 'Start event',
    ring: 'start',
    throw: false,
    markers: ['message', 'timer', 'conditional', 'signal', 'multiple', 'parallel_multiple'],
  },
  start_non_interrupting: {
    label: 'Non-interrupting start event',
    ring: 'start-dashed',
    throw: false,
    markers: [
      'message',
      'timer',
      'escalation',
      'conditional',
      'signal',
      'multiple',
      'parallel_multiple',
    ],
    noPlain: true,
  },
  start_event_subprocess: {
    label: 'Event sub-process start event',
    ring: 'start',
    throw: false,
    markers: ['error', 'escalation', 'compensation'],
    noPlain: true,
  },
  intermediate_catch: {
    label: 'Intermediate catch event',
    ring: 'double',
    throw: false,
    markers: [
      'message',
      'timer',
      'error',
      'escalation',
      'cancel',
      'compensation',
      'conditional',
      'link',
      'signal',
      'multiple',
      'parallel_multiple',
    ],
  },
  intermediate_catch_non_interrupting: {
    label: 'Non-interrupting boundary event',
    ring: 'double-dashed',
    throw: false,
    markers: [
      'message',
      'timer',
      'escalation',
      'conditional',
      'signal',
      'multiple',
      'parallel_multiple',
    ],
    noPlain: true,
  },
  intermediate_throw: {
    label: 'Intermediate throw event',
    ring: 'double',
    throw: true,
    markers: ['message', 'escalation', 'compensation', 'link', 'signal', 'multiple'],
  },
  end: {
    label: 'End event',
    ring: 'end',
    throw: true,
    markers: [
      'message',
      'error',
      'escalation',
      'cancel',
      'compensation',
      'signal',
      'terminate',
      'multiple',
    ],
  },
};

const ring = (kind) => {
  const dash = kind.endsWith('dashed') ? ' stroke-dasharray="4.5 3"' : '';
  if (kind === 'end')
    return `<circle cx="20" cy="20" r="17.5" fill="${PAPER}" stroke="${INK}" stroke-width="3.5"/>`;
  if (kind.startsWith('double'))
    return (
      `<circle cx="20" cy="20" r="18.5" fill="${PAPER}" stroke="${INK}" stroke-width="1.3"${dash}/>` +
      `<circle cx="20" cy="20" r="15.5" fill="none" stroke="${INK}" stroke-width="1.3"${dash}/>`
    );
  return `<circle cx="20" cy="20" r="18.5" fill="${PAPER}" stroke="${INK}" stroke-width="1.5"${dash}/>`;
};

for (const [pos, p] of Object.entries(POSITIONS)) {
  const base = `${pos}_event`;
  if (!p.noPlain)
    add(
      base,
      { label: p.label, category: 'Events', kind: 'event', keywords: ['event'] },
      40,
      40,
      ring(p.ring),
    );
  for (const m of p.markers) {
    add(
      `${base}_${m}`,
      {
        label: `${words(m)} ${p.label.toLowerCase()}`,
        category: 'Events',
        kind: 'event',
        keywords: ['event', m],
      },
      40,
      40,
      ring(p.ring) + MARKERS[m](p.throw),
    );
  }
}

// ------------------------------------------------------------------ activities

const ICONS = {
  task: '',
  user_task:
    `<circle cx="13" cy="10.5" r="3.6" fill="${PAPER}" stroke="${INK}" stroke-width="1.1"/>` +
    `<path d="M6 21 V18.5 C6 15.5 8.5 14 11 14 H15 C17.5 14 20 15.5 20 18.5 V21 Z" fill="${PAPER}" stroke="${INK}" stroke-width="1.1"/>`,
  service_task:
    [0, 45, 90, 135]
      .map(
        (a) =>
          `<rect x="11.6" y="5.5" width="2.8" height="15" fill="${INK}" transform="rotate(${a} 13 13)"/>`,
      )
      .join('') +
    `<circle cx="13" cy="13" r="5.2" fill="${INK}"/><circle cx="13" cy="13" r="2.2" fill="${PAPER}"/>`,
  send_task: `<rect x="6" y="8" width="15" height="10" fill="${INK}"/><path d="M6 8 L13.5 13.5 L21 8" fill="none" stroke="${PAPER}" stroke-width="1.1"/>`,
  receive_task: `<rect x="6" y="8" width="15" height="10" fill="${PAPER}" stroke="${INK}" stroke-width="1.1"/><path d="M6 8 L13.5 13.5 L21 8" fill="none" stroke="${INK}" stroke-width="1.1"/>`,
  manual_task: `<path d="M6 12.5 C6 10.5 8 9.5 9.5 9.5 H18.5 C19.6 9.5 19.6 11.2 18.5 11.2 H13.5 H20.5 C21.6 11.2 21.6 13 20.5 13 H14 H20 C21.1 13 21.1 14.8 20 14.8 H14 H19 C20 14.8 20 16.5 19 16.5 H9.5 C7.5 16.5 6 15 6 12.5 Z" fill="${PAPER}" stroke="${INK}" stroke-width="1" stroke-linejoin="round"/>`,
  business_rule_task: `<rect x="6" y="7" width="16" height="12" fill="${PAPER}" stroke="${INK}" stroke-width="1.1"/><rect x="6" y="7" width="16" height="3.2" fill="${INK}"/><path d="M6 14.5 H22 M10.5 10.2 V19" stroke="${INK}" stroke-width="1"/>`,
  script_task: `<path d="M9 6.5 H19 C16.5 9 16.5 11.5 19 14 C21.5 16.5 21.5 18 19 20.5 H9 C11.5 18 11.5 16.5 9 14 C6.5 11.5 6.5 9 9 6.5 Z" fill="${PAPER}" stroke="${INK}" stroke-width="1.1"/><path d="M10.2 10 H16.4 M11.4 13.3 H17.4 M11.4 16.6 H17.6" stroke="${INK}" stroke-width="1"/>`,
};

/** Bottom markers; `x` is the centre. */
const ACTIVITY_MARKERS = {
  loop: (x) =>
    `<path d="M${x - 4.5} 70.5 A5.5 5.5 0 1 1 ${x + 2.5} 72.2" fill="none" stroke="${INK}" stroke-width="1.3"/><path d="M${x - 7.2} 69.2 L${x - 4.5} 71.6 L${x - 2.4} 68.6" fill="none" stroke="${INK}" stroke-width="1.3"/>`,
  mi_parallel: (x) =>
    `<path d="M${x - 4.5} 63 V74 M${x} 63 V74 M${x + 4.5} 63 V74" stroke="${INK}" stroke-width="1.8"/>`,
  mi_sequential: (x) =>
    `<path d="M${x - 5.5} 64 H${x + 5.5} M${x - 5.5} 68.5 H${x + 5.5} M${x - 5.5} 73 H${x + 5.5}" stroke="${INK}" stroke-width="1.8"/>`,
  compensation: (x) =>
    `<path d="M${x - 7} 68.5 L${x} 64 V73 Z M${x} 68.5 L${x + 7} 64 V73 Z" fill="none" stroke="${INK}" stroke-width="1.2" stroke-linejoin="round"/>`,
};
const plus = (x, y = 62.5) =>
  `<rect x="${x - 6}" y="${y}" width="12" height="12" fill="none" stroke="${INK}" stroke-width="1.2"/><path d="M${x} ${y + 2.5} V${y + 9.5} M${x - 3.5} ${y + 6} H${x + 3.5}" stroke="${INK}" stroke-width="1.3"/>`;
const tilde = (x) =>
  `<path d="M${x - 6} 69.5 C${x - 4} 66 ${x - 2} 66 ${x} 68.5 C${x + 2} 71 ${x + 4} 71 ${x + 6} 67.5" fill="none" stroke="${INK}" stroke-width="1.4"/>`;

const box = (opts = {}) =>
  `<rect x="1" y="1" width="98" height="78" rx="10" fill="${PAPER}" stroke="${INK}" stroke-width="${opts.width ?? 1.5}"${opts.dash ? ' stroke-dasharray="3 3"' : ''}/>` +
  (opts.double
    ? `<rect x="4.5" y="4.5" width="91" height="71" rx="8" fill="none" stroke="${INK}" stroke-width="1.2"/>`
    : '');

const TASKS = Object.keys(ICONS);
const ACTIVITY_TYPES = {
  ...Object.fromEntries(
    TASKS.map((t) => [t, { label: words(t), body: box() + ICONS[t], kind: 'task' }]),
  ),
  call_activity: { label: 'Call activity', body: box({ width: 3.5 }), kind: 'task' },
  subprocess: { label: 'Sub-process', body: box(), kind: 'subprocess', collapsed: plus },
  event_subprocess: {
    label: 'Event sub-process',
    body: box({ dash: true }),
    kind: 'subprocess',
    collapsed: plus,
  },
  transaction: {
    label: 'Transaction',
    body: box({ double: true }),
    kind: 'subprocess',
    collapsed: plus,
  },
  ad_hoc_subprocess: {
    label: 'Ad hoc sub-process',
    body: box(),
    kind: 'subprocess',
    collapsed: plus,
    adHoc: true,
  },
  call_subprocess: {
    label: 'Call activity (sub-process)',
    body: box({ width: 3.5 }),
    kind: 'subprocess',
    collapsed: plus,
  },
};

for (const [type, a] of Object.entries(ACTIVITY_TYPES)) {
  for (const marker of ['', ...Object.keys(ACTIVITY_MARKERS)]) {
    // Marker row, centred: [marker] [+] [~]
    const parts = [
      ...(marker ? [ACTIVITY_MARKERS[marker]] : []),
      ...(a.collapsed ? [a.collapsed] : []),
      ...(a.adHoc ? [tilde] : []),
    ];
    const step = 16;
    const first = 50 - ((parts.length - 1) * step) / 2;
    const row = parts.map((f, i) => f(first + i * step)).join('');
    const id = marker ? `${type}_${marker}` : type;
    add(
      id,
      {
        label: `${a.label}${marker ? ` (${words(marker).toLowerCase().replace('mi ', 'multi-instance ')})` : ''}`,
        category: a.kind === 'task' ? 'Activities' : 'Sub-processes',
        kind: a.kind,
        labelPosition: 'inside',
        keywords: ['activity', ...(marker ? [marker] : [])],
      },
      100,
      80,
      a.body + row,
    );
  }
}

// ------------------------------------------------------------------ gateways

const diamond = `<path d="M25 1 L49 25 L25 49 L1 25 Z" fill="${PAPER}" stroke="${INK}" stroke-width="1.5" stroke-linejoin="round"/>`;
const thick = (d) => `<path d="${d}" fill="none" stroke="${INK}" stroke-width="3.5"/>`;
const pentagon = `<path d="M25 18.5 L31.2 23 L28.8 30.3 H21.2 L18.8 23 Z" fill="none" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/>`;
const GATEWAYS = {
  exclusive_gateway: ['Exclusive gateway', thick('M18 18 L32 32 M32 18 L18 32')],
  exclusive_gateway_plain: ['Exclusive gateway (no marker)', ''],
  inclusive_gateway: [
    'Inclusive gateway',
    `<circle cx="25" cy="25" r="10" fill="none" stroke="${INK}" stroke-width="3"/>`,
  ],
  parallel_gateway: ['Parallel gateway', thick('M25 14 V36 M14 25 H36')],
  complex_gateway: [
    'Complex gateway',
    thick('M25 14 V36 M14 25 H36 M17.2 17.2 L32.8 32.8 M32.8 17.2 L17.2 32.8'),
  ],
  event_based_gateway: [
    'Event-based gateway',
    `<circle cx="25" cy="25" r="12" fill="none" stroke="${INK}" stroke-width="1.2"/><circle cx="25" cy="25" r="9.5" fill="none" stroke="${INK}" stroke-width="1.2"/>${pentagon}`,
  ],
  event_based_gateway_start: [
    'Exclusive event-based start gateway',
    `<circle cx="25" cy="25" r="12" fill="none" stroke="${INK}" stroke-width="1.2"/>${pentagon}`,
  ],
  parallel_event_based_gateway: [
    'Parallel event-based start gateway',
    `<circle cx="25" cy="25" r="12" fill="none" stroke="${INK}" stroke-width="1.2"/><path d="M23.3 17 H26.7 V23.3 H33 V26.7 H26.7 V33 H23.3 V26.7 H17 V23.3 H23.3 Z" fill="none" stroke="${INK}" stroke-width="1.1"/>`,
  ],
};
for (const [id, [label, body]] of Object.entries(GATEWAYS))
  add(
    id,
    { label, category: 'Gateways', kind: 'gateway', keywords: ['gateway', 'decision'] },
    50,
    50,
    diamond + body,
  );

// ------------------------------------------------------------------ data, artifacts, conversations

const page = `<path d="M1 1 H25 L35 11 V49 H1 Z" fill="${PAPER}" stroke="${INK}" stroke-width="1.3" stroke-linejoin="round"/><path d="M25 1 V11 H35" fill="none" stroke="${INK}" stroke-width="1.3"/>`;
const collection = `<path d="M13.5 38 V47 M18 38 V47 M22.5 38 V47" stroke="${INK}" stroke-width="1.6"/>`;
const arrow = (f) =>
  `<path d="M4 7.5 H10 V4.5 L15.5 9.5 L10 14.5 V11.5 H4 Z" fill="${f ? INK : PAPER}" stroke="${INK}" stroke-width="1.1" stroke-linejoin="round"/>`;
add(
  'data_object',
  { label: 'Data object', category: 'Data', kind: 'data', keywords: ['data'] },
  36,
  50,
  page,
);
add(
  'data_object_collection',
  { label: 'Data collection', category: 'Data', kind: 'data', keywords: ['data', 'list'] },
  36,
  50,
  page + collection,
);
add(
  'data_input',
  { label: 'Data input', category: 'Data', kind: 'data', keywords: ['data'] },
  36,
  50,
  page + arrow(false),
);
add(
  'data_input_collection',
  { label: 'Data input collection', category: 'Data', kind: 'data' },
  36,
  50,
  page + arrow(false) + collection,
);
add(
  'data_output',
  { label: 'Data output', category: 'Data', kind: 'data', keywords: ['data'] },
  36,
  50,
  page + arrow(true),
);
add(
  'data_output_collection',
  { label: 'Data output collection', category: 'Data', kind: 'data' },
  36,
  50,
  page + arrow(true) + collection,
);
add(
  'data_store',
  { label: 'Data store', category: 'Data', kind: 'database', keywords: ['database', 'store'] },
  50,
  50,
  `<path d="M1 8 V42 C1 46 12 49 25 49 C38 49 49 46 49 42 V8" fill="${PAPER}" stroke="${INK}" stroke-width="1.3"/><ellipse cx="25" cy="8" rx="24" ry="7" fill="${PAPER}" stroke="${INK}" stroke-width="1.3"/><path d="M1 12 C1 16 12 19 25 19 C38 19 49 16 49 12 M1 16 C1 20 12 23 25 23 C38 23 49 20 49 16" fill="none" stroke="${INK}" stroke-width="1.3"/>`,
);
add(
  'message',
  { label: 'Message', category: 'Data', kind: 'data', keywords: ['envelope'] },
  36,
  24,
  `<rect x="1" y="1" width="34" height="22" fill="${PAPER}" stroke="${INK}" stroke-width="1.3"/><path d="M1 1 L18 13 L35 1" fill="none" stroke="${INK}" stroke-width="1.3"/>`,
);
add(
  'text_annotation',
  {
    label: 'Text annotation',
    category: 'Artifacts',
    kind: 'annotation',
    labelPosition: 'inside',
    keywords: ['note', 'comment'],
  },
  100,
  30,
  `<rect x="0" y="0" width="100" height="30" fill="none"/><path d="M12 1 H1.5 V29 H12" fill="none" stroke="${INK}" stroke-width="1.3"/>`,
);
add(
  'group',
  {
    label: 'Group',
    category: 'Artifacts',
    kind: 'group',
    labelPosition: 'inside',
    keywords: ['category'],
  },
  200,
  120,
  `<rect x="1" y="1" width="198" height="118" rx="8" fill="none" stroke="${INK}" stroke-width="1.3" stroke-dasharray="8 3 2 3"/>`,
);
const hexagon = (w) =>
  `<path d="M10.5 1 H29.5 L39 18 L29.5 35 H10.5 L1 18 Z" fill="${PAPER}" stroke="${INK}" stroke-width="${w}" stroke-linejoin="round"/>`;
add(
  'conversation',
  { label: 'Conversation', category: 'Conversations', kind: 'conversation' },
  40,
  36,
  hexagon(1.5),
);
add(
  'call_conversation',
  { label: 'Call conversation', category: 'Conversations', kind: 'conversation' },
  40,
  36,
  hexagon(3.5),
);
add(
  'sub_conversation',
  { label: 'Sub-conversation', category: 'Conversations', kind: 'conversation' },
  40,
  36,
  hexagon(1.5) +
    `<rect x="15" y="22" width="10" height="10" fill="none" stroke="${INK}" stroke-width="1.1"/><path d="M20 24 V30 M17 27 H23" stroke="${INK}" stroke-width="1.1"/>`,
);
add(
  'choreography_task',
  { label: 'Choreography task', category: 'Choreographies', kind: 'task', labelPosition: 'inside' },
  100,
  80,
  `<rect x="1" y="1" width="98" height="78" rx="10" fill="${PAPER}" stroke="${INK}" stroke-width="1.5"/>` +
    `<path d="M1 18 H99 M1 62 H99" stroke="${INK}" stroke-width="1.2"/>`,
);
add(
  'sub_choreography',
  {
    label: 'Sub-choreography',
    category: 'Choreographies',
    kind: 'subprocess',
    labelPosition: 'inside',
  },
  100,
  80,
  `<rect x="1" y="1" width="98" height="78" rx="10" fill="${PAPER}" stroke="${INK}" stroke-width="1.5"/><path d="M1 18 H99 M1 62 H99" stroke="${INK}" stroke-width="1.2"/>` +
    plus(50, 47),
);

// ------------------------------------------------------------------ manifest

const typeMapping = {
  startEvent: 'start_event',
  endEvent: 'end_event',
  intermediateCatchEvent: 'intermediate_catch_event',
  intermediateThrowEvent: 'intermediate_throw_event',
  boundaryEvent: 'intermediate_catch_event',
  task: 'task',
  userTask: 'user_task',
  serviceTask: 'service_task',
  sendTask: 'send_task',
  receiveTask: 'receive_task',
  manualTask: 'manual_task',
  businessRuleTask: 'business_rule_task',
  scriptTask: 'script_task',
  callActivity: 'call_activity',
  subProcess: 'subprocess',
  transaction: 'transaction',
  adHocSubProcess: 'ad_hoc_subprocess',
  exclusiveGateway: 'exclusive_gateway',
  inclusiveGateway: 'inclusive_gateway',
  parallelGateway: 'parallel_gateway',
  complexGateway: 'complex_gateway',
  eventBasedGateway: 'event_based_gateway',
  dataObject: 'data_object',
  dataObjectReference: 'data_object',
  dataInput: 'data_input',
  dataOutput: 'data_output',
  dataStoreReference: 'data_store',
  dataStore: 'data_store',
  message: 'message',
  textAnnotation: 'text_annotation',
  group: 'group',
  conversation: 'conversation',
  callConversation: 'call_conversation',
  subConversation: 'sub_conversation',
  choreographyTask: 'choreography_task',
  subChoreography: 'sub_choreography',
  callChoreography: 'choreography_task',
};

mkdirSync(join(root, 'fragments'), { recursive: true });
writeFileSync(
  join(root, 'fragments', 'approval.draw'),
  `\${start} = event "Request received" shape \${alias}.start_event_message
\${review} = task "Review request" shape \${alias}.user_task
\${decide} = gateway "Approved?" shape \${alias}.exclusive_gateway
\${notify} = task "Notify requester" shape \${alias}.send_task
\${done} = event "Done" shape \${alias}.end_event
\${start} -> \${review}
\${review} -> \${decide}
\${decide} -> \${notify} "yes"
\${decide} -> \${review} "no"
\${notify} -> \${done}
`,
);
writeFileSync(
  join(root, 'fragments', 'timeout.draw'),
  `\${task} = task "\${task_name}" shape \${alias}.user_task
\${timer} = event "\${timeout}" shape \${alias}.intermediate_catch_event_timer prop "bpmn.attachedTo" "\${task}"
\${escalate} = task "Escalate" shape \${alias}.task
\${timer} -> \${escalate}
`,
);

const manifest = {
  name: 'bpmn',
  version: '1.0.0',
  description:
    'BPMN 2.0 shapes: events with every marker, activities with type icons and loop/multi-instance/compensation markers, gateways, data, artifacts, conversations and choreographies.',
  license: 'MIT',
  author: 'DiaDraw',
  source: { kind: 'original' },
  shapes,
  composites: {
    task_with_timer: {
      label: 'Task with a timer boundary event',
      category: 'Activities',
      kind: 'task',
      width: 110,
      height: 96,
      parts: [
        { shape: 'user_task', x: 0, y: 0, width: 100, height: 80 },
        { shape: 'intermediate_catch_event_timer', x: 64, y: 62, width: 32, height: 32 },
      ],
    },
  },
  fragments: {
    approval: {
      label: 'Approval loop',
      category: 'Fragments',
      description: 'A message start, a review task and an exclusive gateway that loops back.',
      params: ['start', 'review', 'decide', 'notify', 'done'].map((name) => ({
        name,
        default: name,
      })),
      text: 'fragments/approval.draw',
    },
    timeout: {
      label: 'Task with a timeout',
      category: 'Fragments',
      description: 'A user task with a timer boundary event leading to an escalation task.',
      params: [
        { name: 'task', default: 'work' },
        { name: 'task_name', default: 'Do the work' },
        { name: 'timer', default: 'late' },
        { name: 'timeout', default: '2 days' },
        { name: 'escalate', default: 'escalate' },
      ],
      text: 'fragments/timeout.draw',
    },
  },
  mappings: { bpmn: typeMapping },
};
writeFileSync(join(root, 'diadraw-library.json'), `${JSON.stringify(manifest, null, 2)}\n`);
console.log(`${Object.keys(shapes).length} shapes`);
