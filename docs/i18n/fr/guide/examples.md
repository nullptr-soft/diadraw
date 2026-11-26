# Exemples

Chaque application fournit des exemples de diagrammes à ouvrir, lire et modifier. Sur macOS, Linux et Windows, ils se trouvent sous **File › Examples** ; dans l’application web, dans la liste **Examples…** de la barre d’outils. L’exemple choisi s’ouvre comme un nouveau document sans titre : vos modifications ne touchent jamais l’original. Enregistrez-le sous un nom de votre choix pour le conserver.

| Exemple | Ce qu’il montre |
| --- | --- |
| `bpmn-01-order-process` | Processus BPMN dans un bassin à deux couloirs : début par message, tâches utilisateur, d’envoi, manuelle et de service, passerelles exclusive et parallèle, événement de bordure temporisé et donnée de sortie. |
| `bpmn-02-claim-collaboration` | Collaboration BPMN entre deux bassins : flux de messages, passerelle événementielle, tâche de règle métier, magasin de données et annotation textuelle. |
| `c4-01` … `c4-04` | Diagrammes C4 de systèmes et de conteneurs, groupes, note libre. |
| `flow-01` … `flow-04` | Organigrammes de traitement avec décisions et boucles. |
| `uml-01-class-library` | Diagramme de classes UML : attributs et opérations, interface, classe abstraite, héritage, réalisation, composition, agrégation et multiplicités. |
| `uml-02-sequence-checkout` | Diagramme de séquence UML : activations, cadres `loop` et `alt`, réponses, message asynchrone et note. |
| `uml-03-activity-order` | Diagramme d’activité UML : nœuds initial et final, décision et fusion correspondante, bifurcation et jonction. |
| `uml-04-state-order` | Machine à états UML : transitions avec événements, gardes et actions ; état composite. |
| `uml-05-use-case-shop` | Diagramme de cas d’utilisation UML : acteurs, frontière du système, «include» et «extend», généralisation d’acteurs. |
| `uml-06-component-shop` | Diagramme de composants UML : composants, interfaces fournies en forme de sucette et interfaces requises. |

Les exemples sont les fichiers `.draw` du dossier `corpus/` du dépôt. Ils servent aussi aux tests et aux mesures de performance : chacun doit être analysé sans avertissement et chaque vue doit pouvoir être agencée en respectant toutes ses indications.

## Comment sont dessinés les exemples UML et BPMN

DiaDraw n’a pas de types de diagrammes réservés à UML ou BPMN ; les exemples utilisent le DSL ordinaire :

- Les **diagrammes de classes** utilisent le type `class` avec `prop "uml.attributes"` et `prop "uml.methods"` (un membre par ligne, séparés par `\n`), ainsi que `prop "stereotype"`. Les multiplicités sont définies sur la relation avec `prop "uml.sourceMultiplicity"` et `prop "uml.targetMultiplicity"`. Les types de traits (héritage, réalisation, composition, etc.) sont définis par des blocs de style de relation ciblant une étiquette, avec des [extrémités](line-styles.md) creuses ou pleines.
- Les **diagrammes de séquence** sont des vues `layout sequence` (voir la [référence du DSL](../dsl.md#vues-de-séquence)).
- Les **diagrammes d’activité et d’états** utilisent des formes de base : `circle` pour les nœuds initial et final, `bar` pour les bifurcations et jonctions, `rhombus` pour les décisions et fusions, `rounded` pour les états.
- Les **diagrammes de cas d’utilisation** dessinent les acteurs avec `shape actor` et les cas avec `shape ellipse`, à l’intérieur d’un élément `system` qui représente la frontière du système.
- **BPMN** utilise la bibliothèque intégrée `bpmn` (`use "bpmn@^1" as bpmn`) pour les événements, tâches, passerelles et données. Les bassins et couloirs sont des formes `swimlane` dans une [vue fixe](fixed-views.md), conformément à la pratique des outils BPMN pour disposer un processus. Un événement de bordure est épinglé sur le bord de sa tâche ; `prop "bpmn.attachedTo"` permet à l’exportateur BPMN de l’y rattacher.
