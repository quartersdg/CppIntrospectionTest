#include "stdafx.h"
#include <vector>
#include <map>
#include "dia2.h"

/* From DIADump */
IDiaDataSource *g_pDiaDataSource;
IDiaSession *g_pDiaSession;
IDiaSymbol *g_pGlobalSymbol;

/* TODO: kill off stl */
typedef std::map<DWORD, BSTR> AnnotionMap;
AnnotionMap Annotations;

/* From DIADump, not robust, and I'm sure I leak a ton */
BSTR GetName(IDiaSymbol *pSymbol)
{
    BSTR bstrName;
    BSTR bstrUndName;

    if (pSymbol->get_name(&bstrName) != S_OK) {
        return 0;
    }

    if (pSymbol->get_undecoratedName(&bstrUndName) == S_OK) {
        if (wcscmp(bstrName, bstrUndName) == 0) {
            return bstrName;
        }
        else {
            return bstrUndName;
        }

        SysFreeString(bstrUndName);
    }

    else {
        return bstrName;
    }

    SysFreeString(bstrName);
}

BSTR GetFunctionName(IDiaSymbol *pSymbol)
{
    BSTR bstrName;

    if (pSymbol->get_name(&bstrName) != S_OK) {
        return 0;
    }
    return bstrName;
}

/* I think DIA is wchar_t only, and i need this because
   __FILE__ comes out all lowercase apparently but pdb
   keeps case */
wchar_t *wcsstri(wchar_t *Text, wchar_t *Search) {
    if (!Text || !Search) return NULL;
    while (*Text) {
        wchar_t *T = Text;
        wchar_t *S = Search;
        while (*T && *S && towupper(*T++) == towupper(*S++))
            ;
        if (!*S)
            return Text;
        Text++;
    }
    return 0;
}

/* I want to make sure I find the right types based on the location
   that the DUMPOBJECT is called. So search for a Compilation Unit that
   matches the __FILE__ at that place. Hard coded some things that seemed
   to match up nicely from DIADump */
IDiaSymbol* FindCompilationUnit(IDiaSymbol *pGlobal, wchar_t *FileName)
{
    IDiaSymbol *CompilationUnitSymbol = NULL;
    IDiaEnumSymbols *pEnumSymbols;

    if (FAILED(pGlobal->findChildren(SymTagCompiland, NULL, nsNone, &pEnumSymbols))) {
        return false;
    }

    IDiaSymbol *pCompiland;
    ULONG celt = 0;

    while (!CompilationUnitSymbol && SUCCEEDED(pEnumSymbols->Next(1, &pCompiland, &celt)) && (celt == 1)) {
        // Find all the symbols defined in this compiland and print their info

        IDiaEnumSymbols *pEnumChildren;

        if (SUCCEEDED(pCompiland->findChildren(SymTagNull, NULL, nsNone, &pEnumChildren))) {
            IDiaSymbol *pSymbol;
            ULONG celtChildren = 0;

            while (SUCCEEDED(pEnumChildren->Next(1, &pSymbol, &celtChildren)) && (celtChildren == 1)) {
                DWORD dwSymTag;
                if (pSymbol->get_symTag(&dwSymTag) == S_OK) {
                    /* Like, the Compiland Env holds the info I need, and */
                    if (dwSymTag == SymTagCompilandEnv) {
                        BSTR SymbolName = GetName(pSymbol);
                        /* Like here, apparently "src" is the file name */
                        if (0 == wcscmp(SymbolName,L"src")) {
                            VARIANT vt = { VT_EMPTY };
                            if (pSymbol->get_value(&vt) == S_OK) {
                                if (vt.vt == VT_BSTR && wcsstri(FileName, vt.bstrVal)) {
                                    CompilationUnitSymbol = pCompiland;
                                    CompilationUnitSymbol->AddRef();
                                    VariantClear((VARIANTARG *)&vt);
                                    break;
                                }
                            }
                            VariantClear((VARIANTARG *)&vt);
                        }
                    }
                }
                pSymbol->Release();
            }

            pEnumChildren->Release();
        }

        pCompiland->Release();
    }

    pEnumSymbols->Release();

    return CompilationUnitSymbol;
}

/* Ditto, to above, make sure we are looking in the right place for the right symbol type */
IDiaSymbol* FindFunction(IDiaSymbol *pGlobal, IDiaSymbol *CompilationUnit, wchar_t *FunctionName)
{
    IDiaSymbol *FunctionSymbol = NULL;

    IDiaEnumSymbols *pEnumChildren;

    if (SUCCEEDED(CompilationUnit->findChildren(SymTagNull, NULL, nsNone, &pEnumChildren))) {
        IDiaSymbol *pSymbol;
        ULONG celtChildren = 0;

        while (SUCCEEDED(pEnumChildren->Next(1, &pSymbol, &celtChildren)) && (celtChildren == 1)) {
            DWORD dwSymTag;
            if (pSymbol->get_symTag(&dwSymTag) == S_OK) {
                if (dwSymTag == SymTagFunction) {
                    BSTR SymbolName = GetFunctionName(pSymbol);
                    if (wcscmp(SymbolName, FunctionName) == 0) {
                        FunctionSymbol = pSymbol;
                        SysFreeString(SymbolName);
                        break;
                    }
                    SysFreeString(SymbolName);
                }
            }
            pSymbol->Release();
        }

        pEnumChildren->Release();
    }

    return FunctionSymbol;
}

/* Find the symbol for the correct file and function where the DUMPOBJECT happened. 
   There could be many with the same name but different scope. */
IDiaSymbol* FindLocalSymbol(IDiaSymbol *pGlobal, wchar_t *File, wchar_t *Function, wchar_t *LocalVariable) {
    IDiaSymbol* LocalSymbol = NULL;

    auto CompilatinUnit = FindCompilationUnit(pGlobal, File);
    auto FunctionSymbol = FindFunction(pGlobal, CompilatinUnit, Function);

    IDiaEnumSymbols *pEnumChildren;

    if (SUCCEEDED(FunctionSymbol->findChildren(SymTagNull, NULL, nsNone, &pEnumChildren))) {
        IDiaSymbol *pChild;
        ULONG celt = 0;
        DWORD dwSymTag;

        while (SUCCEEDED(pEnumChildren->Next(1, &pChild, &celt)) && (celt == 1)) {
            if (pChild->get_symTag(&dwSymTag) != S_OK) {
                wprintf(L"ERROR - PrintSymbol get_symTag() failed\n");
                return 0;
            }
            else {
                if (SymTagData == dwSymTag) {
                    BSTR ChildName = GetName(pChild);
                    if (0 == wcscmp(ChildName, LocalVariable)) {
                        LocalSymbol = pChild;
                        break;
                    }
                }
            }
            pChild->Release();
        }

        pEnumChildren->Release();
    }

    CompilatinUnit->Release();
    FunctionSymbol->Release();
    return LocalSymbol;
}

/* TODO: A, fix this abomination of reuse of the same fucking name over and over
   B, fill in some gaps
   C, fix the recursion on types and data */
void DumpObject(IDiaSymbol *pGlobal, IDiaSymbol *pType, uint8_t* Object, BSTR Annotation);

void DumpObjectType(IDiaSymbol *pType, uint8_t* Object, bool PointedAt, BSTR Annotation)
{
    DWORD dwTag;
    ULONGLONG ulLen;
    bool Unsigned = false;

    pType->get_length(&ulLen);

    if (pType->get_symTag(&dwTag) != S_OK) {
        wprintf(L"ERROR - can't retrieve the symbol's SymTag\n");
        return;
    }
    switch (dwTag) {
    case SymTagPointerType: {
        IDiaSymbol *pBaseType;
        if (pType->get_type(&pBaseType) != S_OK) {
            wprintf(L"ERROR - SymTagPointerType get_type");
            return;
        }

        uint8_t *ObjectSubPointer = *(uint8_t**)Object;
        if (ObjectSubPointer) {
            DumpObjectType(pBaseType, ObjectSubPointer, true, Annotation);
        } else {
            printf("(null)");
        }
        pBaseType->Release();
    } break;
    case SymTagBaseType: {
        DWORD dwInfo;
        if (pType->get_baseType(&dwInfo) != S_OK) {
            wprintf(L"SymTagBaseType get_baseType\n");
            return;
        }

        switch (dwInfo) {
        case btChar: {
            if (PointedAt) {
                if (Annotation) {
                    if (0 == wcscmp(L"annotate_string", Annotation)) {
                        wprintf(L"\"%S\"", (char*)Object);
                        return;
                    }
                }
            }
            wprintf(L"'%c'", *(char*)Object);
        }break;

        case btUInt:
            Unsigned = true;
        case btInt:
            switch (ulLen) {
            case 1:
                /* These are the types of things to fill out, not too hard
                    same as the case 4 */
                if (dwInfo == btInt) {
                    wprintf(L"signed ");
                }

                wprintf(L"char");
                break;

            case 2:
                wprintf(L"short");
                break;

            case 4:
                if (Unsigned) {
                    wprintf(L"%u", *(uint32_t*)Object);
                } else {
                    wprintf(L"%d", *(int32_t*)Object);
                }
                break;

            case 8:
                wprintf(L"__int64");
                break;
            }

            dwInfo = 0xFFFFFFFF;
            break;

        case btFloat:
            switch (ulLen) {
            case 4:
                wprintf(L"float");
                break;

            case 8:
                wprintf(L"double");
                break;
            }

            dwInfo = 0xFFFFFFFF;
            break;
        }

        if (dwInfo == 0xFFFFFFFF) {
            break;
        }
    } break;
    case SymTagAnnotation: {
        /* WTF is up with annotations??? */
        printf("annotation");
    } break;
    case SymTagUDT: {
        /* Recurse back to the beginning */
        DumpObject(g_pGlobalSymbol, pType, Object, Annotation);
    } break;
    }
}

void DumpObjectData(IDiaSymbol *pSymbol, uint8_t* Object, bool IsPointedAt, BSTR Annotation) {
    IDiaSymbol *pType;
    if (pSymbol->get_type(&pType) == S_OK) {
        DumpObjectType(pType, Object, IsPointedAt, Annotation);
    }
}

void DumpObject(IDiaSymbol *pGlobal, IDiaSymbol *pType, uint8_t* Object, BSTR Annotation) {
    DWORD dwSymTag;

    if (pType->get_symTag(&dwSymTag) != S_OK) {
        return;
    }

    switch (dwSymTag) {
    case SymTagData:
        DumpObjectData(pType, Object, false, Annotation);
        break;
    case SymTagAnnotation: {
        printf("annotation");
    } break;

    case SymTagUDT: {
        IDiaEnumSymbols *pEnumChildren;

        if (SUCCEEDED(pType->findChildren(SymTagNull, NULL, nsNone, &pEnumChildren))) {
            wprintf(L"{");
            IDiaSymbol *pChild;
            ULONG celt = 0;
            bool first = true;
            DWORD NextSection = -1;

            while (SUCCEEDED(pEnumChildren->Next(1, &pChild, &celt)) && (celt == 1)) {
                DWORD dwLocType, dwBitPos;
                /*DWORD dwRVA, dwSect, dwOff, dwReg, , dwSlot;*/
                LONG lOffset;
                ULONGLONG ulLen;
                VARIANT vt = { VT_EMPTY };

                if (!first && NextSection==-1) {
                    wprintf(L",");
                } else {
                    first = false;
                }

                if (pChild->get_locationType(&dwLocType) != S_OK) {
                    continue;
                }

                switch (dwLocType) {
                case LocIsThisRel:
                    if (pChild->get_offset(&lOffset) == S_OK) {
                        BSTR Annotation = NULL;
                        /* If the previous field had an annotation, then find
                           the name, and pass it around for this next field. */
                        if (NextSection != -1 && (Annotations.end() != Annotations.find(NextSection))) {
                            Annotation = Annotations[NextSection];
                        }
                        /* And reset */
                        NextSection = -1;
                        DumpObject(pGlobal, pChild, Object + lOffset, Annotation);
                    }
                    break;

                case LocIsBitField:
                    if ((pChild->get_offset(&lOffset) == S_OK) &&
                        (pChild->get_bitPosition(&dwBitPos) == S_OK) &&
                        (pChild->get_length(&ulLen) == S_OK)) {
                    }
                    break;
                    
                /* The magic struct member functions are the "annotations", and they
                    show up as Static */
                case LocIsStatic: {
                    BSTR Name = GetName(pChild);
                    if (0 == wcsncmp(Name, L"annotate_", strlen("annotate_"))) {
                        IDiaSymbol *pCoff;
                        /* The COFF Group is the section that the annotation's name lives in
                           which is then also the same as the field here, so save it
                           away to look up later */
                        pChild->get_coffGroup(&pCoff);
                        DWORD dwISect, dwOffset;
                        if (pCoff->get_addressSection(&dwISect) == S_OK) {
                            NextSection = dwISect;
                        }
                    }
                    break;
                }
                }
                pChild->Release();
            }

            pEnumChildren->Release();
            wprintf(L"}\n");
        }
    } break;
    }
}

void DumpObject(IDiaSymbol *pGlobal, wchar_t *File, wchar_t *Function, wchar_t *LocalVariableType, uint8_t* LocalVariable, BSTR Annotation = NULL) {

    IDiaSymbol* LocalSymbol = FindLocalSymbol(pGlobal, File, Function, LocalVariableType);
    if (LocalSymbol) {
        IDiaSymbol *pType;
        if (LocalSymbol->get_type(&pType) == S_OK) {
            DumpObject(pGlobal, pType, LocalVariable, Annotation);
        }
    }
}

void AddAnnotation(BSTR Name, DWORD Section) {
    Annotations[Section] = Name;
}

/* Search through all the Sections of all the compilands. Anything that starts with
   annotate_ is an annotation. It's section number is assigned to fields that are
   annotated with the same string */
void FindAllAnnotationSections(IDiaSymbol *pGlobal) {
    
    IDiaEnumSymbols *pEnumSymbols;

    if (FAILED(pGlobal->findChildren(SymTagCompiland, NULL, nsNone, &pEnumSymbols))) {
        return;
    }

    IDiaSymbol *pCompiland;
    ULONG celt = 0;

    while (SUCCEEDED(pEnumSymbols->Next(1, &pCompiland, &celt)) && (celt == 1)) {
        IDiaEnumSymbols *pEnumChildren;

        if (SUCCEEDED(pCompiland->findChildren(SymTagCoffGroup, NULL, nsNone, &pEnumChildren))) {
            IDiaSymbol *pSymbol;
            ULONG celtChildren = 0;

            while (SUCCEEDED(pEnumChildren->Next(1, &pSymbol, &celtChildren)) && (celtChildren == 1)) {
                BSTR Name = GetName(pSymbol);

                if (0 == wcsncmp(Name, L"annotate_", strlen("annotate_"))) {
                    DWORD dwISect, dwOffset;
                    if (pSymbol->get_addressSection(&dwISect) == S_OK) {
                        AddAnnotation(Name, dwISect);
                    }
                } else {
                    SysFreeString(Name);
                }
                pSymbol->Release();
            }

            pEnumChildren->Release();
        }

        pCompiland->Release();
    }

    pEnumSymbols->Release();
}

#define WIDEI(x) L##x
#define WIDE(x) WIDEI(x)


#define DUMPOBJECT(LocalVariable) \
    DumpObject(g_pGlobalSymbol,WIDE(__FILE__),WIDE(__FUNCTION__),WIDE(#LocalVariable),(uint8_t*)&LocalVariable)

#define STRINGIFYI(a) #a
#define STRINGIFY(a) STRINGIFYI(a)
#define CONCATI(a,b) a ## b
#define CONCAT(a,b) CONCATI(a,b)


/* Here's where the annotate "magic" happens.
   The result of all this is this excerpt from the .map


    Preferred load address is 00400000

    Start         Length     Name                   Class
    ...
    0006:00000000 00000132H annotate_string         DATA
    ....
    Address         Publics by Value              Rva+Base       Lib:Object
    ...
    0006:00000000       ?annotate_0@Person@@QAEXXZ 0042e000 f i IntrospectionTest.obj

    Notice that annotate_0 shows up in the enumeration of childre of Person.
    And I can then look up which section it lives in, which is 6, and then I can look
    up the name of the section, wich is annotate_string, and then, if I bothered to do
    some real work, once I start working on the very next field, I can treat it as a
    "string"
*/

#define ANNOTATE(a)                                                     \
    __pragma(section(STRINGIFY(CONCAT(annotate_,a)), read,write,execute))   \
    void __declspec(code_seg(STRINGIFY(CONCAT(annotate_,a)))) __declspec(dllexport) CONCAT(annotate_,__COUNTER__) (void) {};  \


typedef struct Person {
    ANNOTATE(string);
    char * Name;
    int Age;
    char *JustAChar;
    Person *Parent;
} Person;


void Test(IDiaSymbol *pGlobal) {
    Person A;
    A.Age = 444;
    A.Name = "steve";
    A.JustAChar = "a string";
    A.Parent = NULL;

    DUMPOBJECT(A);

    Person B;
    B.Age = 33;
    B.Name = "0x12345678";
    B.JustAChar = "b string";
    B.Parent = &A;

    DUMPOBJECT(B);
}

/* Almost completely from DIADump */
int wmain(int argc, wchar_t *argv[]) {
    HRESULT hr = CoInitialize(NULL);

    hr = CoCreateInstance(__uuidof(DiaSource),
        NULL,
        CLSCTX_INPROC_SERVER,
        __uuidof(IDiaDataSource),
        (void **)&g_pDiaDataSource);

    if (FAILED(hr)) {
        wprintf(L"CoCreateInstance failed - HRESULT = %08X\n", hr);
        return false;
    }

    hr = g_pDiaDataSource->loadDataFromPdb(argv[1]);
    if (FAILED(hr)) {
        wprintf(L"Load data failed - HRESULT = %08X\n", hr);
        return false;
    }

    hr = g_pDiaDataSource->openSession(&g_pDiaSession);
    if (FAILED(hr)) {
        wprintf(L"openSession failed - HRESULT = %08X\n", hr);
        return false;
    }
    hr = g_pDiaSession->get_globalScope(&g_pGlobalSymbol);
    if (FAILED(hr)) {
        wprintf(L"get_globalScope - HRESULT = %08X\n", hr);
        return false;
    }

    FindAllAnnotationSections(g_pGlobalSymbol);
    Test(g_pGlobalSymbol);

    return 0;
}

