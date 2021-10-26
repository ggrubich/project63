Projekt zespołowy - interpreter języka

Jego zadaniem będzie interpretowanie języków obiektowych, np. python, javascript na nasz własny.
Język, w którym tworzony będzie interpreter - c++.



# Opis języka

## Wyrażenia

### Literały
Język wspiera literały liczb całkowitych (np, `13`), liczb zmiennoprzecinowych (np. `12.34`) i łańcuchów znaków (np. `"test"`).

### Zmienne
Zmienne deklarowane są przy pomocy operatora `let` po którym następuje nazwa zmiennej, znak `=` i jej początowna wartość. Przypiwywanie do nich wartości jest wykonywane samym operatorem `=`. Odwołania do zmiennych niezadeklarowanych powodują błąd kompilacji.
```
let x = 13;
x = x + 1;
y = 10;  // błąd - y nie zadeklarowane
```

### Sekwencje i wyrażenia puste
Sekwencja wyrażeń składa się z n wyrażeń oddzielonych od siebie średnikiem, która zwraca wartość ostatniego wyrażenia. Np:
```
foo;
bar;
baz
```
dokona ewaluacji wyrażeń foo, bar i baz i zwróci wartość wyrażnia baz.

Wyrażnie puste to wyrażenie składające się z samych znaków białych. Zwraca ono wartość `nil`.

### Bloki
Blok jest strukutą pozwalającą na wprowadzenie nowego zakresu leksykalnego dla zmiennych. Oznacza to, że zmienne zadeklarowane wewnątrz bloku nie są dostępne w blokach zewnętrznych, ale można ich używać w blokach zagnieżdżnoych wewnątrz, np:
```
{
    let x = 13;
    println(x);  // poprawne
    {
        println(x);  // też poprawne
    }
}
println(x);  // niepoprawne - x nie zadeklarowany w tym bloku
```
`if`, `while` oraz inne struktury w których kod jest otoczony nawiasami klamrowymi domyślnie wprowadzają nowy blok.

### defer
`defer` pozwala na określenie kodu który zostanie wykonany podczas wychodzenia z obecnego bloku leksykalnego. `defer` wykonywane są w kolejności odwrotnej od ich deklaracji:
```
{
    defer {
        println("foo");
    };
    defer {
        println("bar");
    };
}  // wyświetla "bar", a następnie "foo"
```

### if
Wyrażenie `if` ewaluuje predykat i wykonuje kod umieszczony w bloku jeśli jest on prawdziwy, np.
```
if pred {
    println("pred jest prawdziwy");
}
```
Po if można opcjonalnie dodać gałąź alternatywną `else` która jest wykonywana jeśli predykat jest fałszywy:
```
if pred {
    println("pred jest prawdziwy");
} else {
    println("pred jest fałszywy");
}
```
Między `if`, a `else` może znajdować się dowolna liczba gałęzi `else if` z predykatami które będą sprawdzane po kolei:
```
if pred1 {
    println("pred1 prawdziwy");
} else if pred2 {
    println("pred2 prawdziwy");
} else if pred3 {
    println("pred3 prawdziwy");
}
```

### while
Pętla wykonywana dopóki, dopóty predykat jest prawdziwy:
```
while pred {
    foo();
}
```
Do przedwczesnego zakończenia pętli może służyć słowo kluczowe `break`, a do natychmiastowego przejścia do następnej iteracji słowo `continue`.

### Funkcje
Funkcje deklarowane są za pomocą słowa kluczowego `fn`, po którym wewnątrz nawiasów wymienaine są argumenty, a następnie blok zawierający ciało funkcji:
```
let add = fn(x, y) {
    x + y
}
```
Funkcja domyślnie zwraca wartość ostatniego wyrażenia w jej ciele. Opcjonalnie można użyć słowa kluczowego `return` aby przedwcześnie zwrócić wartość. `return` bez argumentów zwraca `nil`:
```
let max = fn(x, y) {
    if x > y {
        return x;
    };
    return y;
}
```
Funkcje wywoływane są poprzez dodanie do nich nawiasów zawierających argumenty:
```
println(max(1, 3))
```
Ciało funkcji może odwoływać się do zmiennych, które występują w zewnętrznym środowisku leksykalnym funkcji ale nie zostały jeszcze zdefiniowane. To umożliwia pisanie funkcji wzajemnie rekrencyjnych bez potrzeby ich ręcznego deklarowania:
```
let foo = fn() {
    bar()
};
let bar = fn() {
    foo()
};
foo()
```
Używanie tego typu zmiennych zanim zostaną one zdefiniowane powoduje błąd podczas wykonywania programu.

### Metody
Metody różnią się od funkcji tym, że posiadają one własny kontekst `self`. Kontekst ten odnosi się do instancji obiektu na którym dana metoda została wywołana. Dla porównania funkcje dziedziczą kontekst `self` bloku leksykalnego w którym zostały zadeklarowane.
Metodę deklarujemy przy pomocy słowa kuczowego `method`. W odróżnieniu od funkcji, nawiasy z argumentami są opcjonalne: metody zadeklarowane z nawiasami będą przyjmować argumenty które należy podać przy ich wywołaniu, z kolei metody bez nawiasów nie przyjmują argumentów (jest to przydatne choćby do pisania getterów).
```
let withArgs = method(x, y) {
    expr
};
let withoutArgs = method {
    expr
};
```
Wywoływanie metod na obiektach jest wykonywane operatorem kropki `.`, np:
```
thing.foo(1, 2);  // wywołuje metodę foo z argumentami 1 i 2
thing.bar;  // wywołuje methodę bezargumentową bar
```
Do kontekstu `self` można odnosić się na dwa sposoby: poprzez słowo kluczowe `self` zwracające instancję na której wywołano metodę oraz poprzez operator `@` którym można odnosić się do zmiennych instancji:
```
method() {
    self.foo();  // wywołuje metodę foo na instancji
    @bar = 13;  // przypisuje zmienną instancji bar
    println(@bar);  // odczytuje zmienną instancji bar
}
```

### Wyjątki
Wyjątki można zgłaszać przy pomocy słowa kluczowego `throw` po którym następuje wartość wyjątku.
Łapanie wyjątków następuje przy pomocy konstrukcji `try` `catch`, w której blok po `try` jest wykonywany do napotkania pierwszego wyjątku, po czym wykonywany jest blok `catch` z tym wyjątkiem przypisanym do nazwy podanej po słowie kluczowym `catch`:
```
try {
    println("running");
    throw SomeException();
    println("not displayed");
} catch ex {
    println(ex);  // prints SomeException
}
```
