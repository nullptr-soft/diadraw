# Diagram patterns

Small, working starting points, one per kind of diagram. Each one checks clean and renders with
the DiaDraw CLI. The DiaDraw repository's `corpus/` folder has fuller examples of each
(`c4-*`, `flow-*`, `uml-01` … `uml-06`, `bpmn-01`, `bpmn-02`).

## Architecture (C4 context and containers)

One model, two views: the system from outside, and its containers.

```
style tag "external" { fill "#999999" }

model {
  customer = person "Customer" description "Buys things online"
  mail = system "Mail service" tag "external"
  shop = system "Shop" {
    web = container "Web app" tech "React"
    api = container "API" tech "Go"
    db  = container "Orders DB" tech "Postgres" shape cylinder
    jobs = container "Jobs" tech "SQS" shape queue
  }
  customer -> shop.web "browses"
  shop.web -> shop.api "calls" tech "JSON/HTTPS"
  shop.api -> shop.db "reads and writes"
  shop.api --> shop.jobs "enqueues"
  shop.api -> mail "sends receipts"
}

view context {
  include customer shop mail
  layout layered direction right
}

view containers of shop {
  include *
  layout layered direction right
  pin db below api
}
```

## Flowchart

```
model {
  begin  = start "Start"
  form   = process "Show login form"
  valid  = decision "Credentials valid?"
  home   = process "Open dashboard"
  failed = process "Show error"
  done   = end "End"

  begin -> form
  form -> valid
  valid -> home "yes"
  valid -> failed "no"
  failed -> form "retry"
  home -> done
}

view login {
  include *
  layout layered direction down
}
```

## Sequence

Messages live in the view; the model only declares the participants.

```
model {
  user = person "User"
  web  = container "Web app"
  api  = container "API"
  db   = database "DB"
}

view login {
  include user web api db
  layout sequence
  sequence {
    autonumber
    user -> web "Log in"
    activate web
    web -> api "POST /session"
    api -> db "find user"
    db --> api "user"
    alt "valid" {
      api --> web "token"
      web --> user "Welcome"
    } else "invalid" {
      api --> web "401"
      web --> user "Error"
    }
    deactivate web
    web ->> api "audit event"
  }
}
```

## UML class diagram

Members go in `prop "uml.attributes"` and `prop "uml.methods"`, one per line with `\n`; the kind
of line comes from tagged relationship styles.

```
style relationship tag "inherits" { endArrow block endFill 0 }
style relationship tag "realizes" { dashed true endArrow block endFill 0 }
style relationship tag "composes" { startArrow diamond startFill 1 endArrow none }

model {
  shape_ = class "Shape" prop "stereotype" "interface" prop "uml.methods" "+ area(): double"
  circle = class "Circle" prop "uml.attributes" "- r: double" prop "uml.methods" "+ area(): double"
  square = class "Square" prop "uml.attributes" "- side: double" prop "uml.methods" "+ area(): double"
  canvas = class "Canvas" prop "uml.methods" "+ add(s: Shape)\n+ draw()"

  circle -> shape_ tag "realizes"
  square -> shape_ tag "realizes"
  canvas -> shape_ "holds" tag "composes" prop "uml.sourceMultiplicity" "1" prop "uml.targetMultiplicity" "0..*"
}

view classes {
  include *
  layout layered direction up
}
```

## UML activity and state diagrams

Initial and final nodes are circles, forks and joins are bars, states are `rounded`.

```
model {
  initial = start "" shape circle style "fill=#1e293b;stroke=#1e293b"
  created = process "Created" shape rounded
  paid    = process "Paid" shape rounded description "entry / reserve stock"
  shipped = process "Shipped" shape rounded
  final   = end "" shape circle style "fill=#ffffff;stroke=#1e293b;strokeWidth=4"

  initial -> created
  created -> paid "pay [amount ok] / send receipt"
  paid -> shipped "handed to carrier"
  created -> final "cancel"
  shipped -> final
}

view lifecycle {
  include *
  layout layered direction right
  size initial 28, 28
  size final 32, 32
}
```

## UML use case diagram

```
style relationship { endArrow none }
style relationship tag "dependency" { dashed true endArrow open }
style person { fill "#ffffff" fontColor "#1e293b" }   // stick figures, named below

model {
  customer = person "Customer" shape actor
  shop = system "Online shop" {
    order = process "Place order" shape ellipse
    pay   = process "Pay" shape ellipse
  }
  customer -> shop.order
  shop.order -> shop.pay "«include»" tag "dependency"
}

view usecases {
  include *
  layout layered direction right spacing 60
  size customer 48, 64
}
```

## BPMN

BPMN uses the built-in `bpmn` library. For a small process, an automatic layout is enough;
for pools and lanes, use a `layout fixed` view with `pin … at … size` as `corpus/bpmn-01-order-process.draw` does.

```
use "bpmn@^1" as bpmn

style relationship { endArrow block endFill 1 }

model {
  received = event "Order received" shape bpmn.start_event_message
  check    = task "Check order" shape bpmn.user_task
  valid    = gateway "Valid?" shape bpmn.exclusive_gateway
  ship     = task "Ship" shape bpmn.service_task
  reject   = task "Reject" shape bpmn.send_task
  shipped  = event "Shipped" shape bpmn.end_event
  rejected = event "Rejected" shape bpmn.end_event

  received -> check
  check -> valid
  valid -> ship "yes"
  valid -> reject "no"
  ship -> shipped
  reject -> rejected
}

view process {
  include *
  layout layered direction right
}
```

## Hand-placed (fixed) diagram

Only when the user wants exact positions.

```
model {
  a = process "Billing"
  b = decision "Paid?" shape rhombus
  a -> b "check"
}

view page1 {
  include *
  layout fixed
  pin a at 40, 40 size 120, 60
  pin b at 240, 30 size 100, 80
  route "a->b" from 1, 0.5 to 0, 0.5
}
```

## Notes, titles and frames

```
view checkout of shop {
  include *
  text "Checkout, container level" style "fontSize=18;fontStyle=1"
  note "Owned by team Web" right of web
  box pci "PCI scope" around api db
}
```
