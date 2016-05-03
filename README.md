# CppIntrospectionTest

Testing out how I could use the Debug Interface Access SDK to debug
the currently running program in order to do some kind of Introspection.

Added in some terrible linker/section nonsense in order to whack up a
quick and dirty annotation thing too.

Here's some inputs and outputs

###sample code

    typedef struct Person {
        ANNOTATE(string);
        char * Name;
        int Age;
        char *JustAChar;
        Person *Parent;
    } Person;
    
    void Test() {
        Person A;
        A.Age = 444;
        A.Name = "steve";
        A.JustAChar = "a string";
        A.Parent = NULL;
    
        Person B;
        B.Age = 33;
        B.Name = "0x12345678";
        B.JustAChar = "b string";
        B.Parent = &A;
    
        DUMPOBJECT(B);
    }

###resulting output
    {"0x12345678",33,'b',{"steve",444,'a',(null)}
    }

###different input code, same introspection code
    typedef struct Thing {
        int x, y;
    };
    typedef struct Person {
        ANNOTATE(string);
        char * Name;
        int Age;
        ANNOTATE(string);
        char *JustAChar;
        Person *Parent;
        Thing T;
    } Person;
    
    void Test() {
        Person A;
        A.Age = 444;
        A.Name = "steve";
        A.JustAChar = "a string";
        A.Parent = NULL;
        A.T.x = 1;
        A.T.y = 2;
    
        Person B;
        B.Age = 33;
        B.Name = "0x12345678";
        B.JustAChar = "b string";
        B.Parent = &A;
        B.T.x = 3;
        B.T.y = 4;
    
        DUMPOBJECT(B);
    }
    
###resulting output
    {"0x12345678",33,"b string",{"steve",444,"a string",(null),{1,2}
    }
    ,{3,4}
    }
