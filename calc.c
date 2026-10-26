// Adeena Rehman 251ADB241
// https://github.com/adeenarehman07/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: AI (Claude), conversation link: https://claude.ai/share/05bc7a50-f199-454f-9901-3579e464504a; Crafting Interpreters "Parsing Expressions" (https://craftinginterpreters.com/parsing-expressions.html); Wikipedia "Common operator notation" (https://en.wikipedia.org/wiki/Common_operator_notation); Wikipedia "Operator associativity" (https://en.wikipedia.org/wiki/Operator_associativity); Wikipedia "Dynamic array" (https://en.wikipedia.org/wiki/Dynamic_array); Wikipedia "Exponentiation by squaring" (https://en.wikipedia.org/wiki/Exponentiation_by_squaring); cppreference fseek, rewind, strtod, realloc (https://cppreference.com/c/io/fseek, https://cppreference.com/c/io/rewind, https://cppreference.com/c/string/byte/strtod, https://cppreference.com/c/memory/realloc); Linux man-pages mkdir(2), opendir(3), readdir(3), stat(2), printf(3) (https://man7.org/linux/man-pages/man2/mkdir.2.html, https://man7.org/linux/man-pages/man3/opendir.3.html, https://man7.org/linux/man-pages/man3/readdir.3.html, https://man7.org/linux/man-pages/man2/stat.2.html, https://man7.org/linux/man-pages/man3/printf.3.html)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define NAME "adeena"
#define LAST "rehman"
#define ID "251ADB241"

typedef struct { int isf; long long i; double d; } Val;

// AI (Claude): prompt "how does a tokenizer keep track of the position of a character in a whole file, not just the line and why do parsers report errors at a certain specifc position" Link: https://claude.ai/share/05bc7a50-f199-454f-9901-3579e464504a
// I used the idea of one index for the whole file, so positions count every newline and comment line.
// The whole file is read into s once and p is an index into it. p is never reset
// per line, so p + 1 is already the 1-based file position (every \n and comment
// character is counted) and we never copy or split the file, so big files stay fast.
static char *s;
static long p;
static long err; // 0 = no error yet, otherwise the 1-based position of the FIRST error
static char *out;
static size_t olen, ocap;

// Keeping the first error only: later functions see err != 0 and just return.
static void fail(long at) { if (!err) err = at + 1; }

static void skip(void) { while (s[p] == ' ' || s[p] == '\t') p++; }

// AI (Claude): prompt "why is doubling size with realloc faster than adding fixed amount each time?" Link: https://claude.ai/share/05bc7a50-f199-454f-9901-3579e464504a
// I used the doubling idea so adding output stays fast.
// Source: Wikipedia "Dynamic array" (https://en.wikipedia.org/wiki/Dynamic_array) and cppreference realloc (https://cppreference.com/c/memory/realloc).
// I used the idea of growing the buffer by a constant factor with realloc, so adding output stays fast.
static void add(const char *t, size_t n) {
    if (olen + n + 1 > ocap) { // doubling the buffer means each byte is copied O(1) times on average
        ocap = (ocap + n + 1) * 2;
        out = realloc(out, ocap);
        if (!out) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    memcpy(out + olen, t, n);
    olen += n;
}

static double dv(Val v) { return v.isf ? v.d : (double)v.i; }

// AI (Claude): prompt "in c how do i do integer division like 7 / 2 = 3 but get 3.5 when one number has a decimal point like 7.0 / 2" Link: https://claude.ai/share/05bc7a50-f199-454f-9901-3579e464504a
// I used the idea of staying in integer arithmetic unless one side is a float.
// Integer arithmetic is used only when BOTH operands are integers; as soon as one
// is a float the whole operation is done in double, which gives 7 / 2 = 3 but 7 / 2.0 = 3.5.
// opAt = start of the operator, rightAt = start of the right operand. Errors about
// the operand (zero divisor, negative exponent) use rightAt; float overflow/NaN
// happens in the operation itself, so it uses opAt.
static Val calc(char op, Val a, Val b, long opAt, long rightAt) {
    Val r = {0, 0, 0};
    if (!a.isf && !b.isf) {
        if (op == '+') r.i = a.i + b.i;
        else if (op == '-') r.i = a.i - b.i;
        else if (op == '*') r.i = a.i * b.i;
        else if (op == '/') { if (b.i == 0) fail(rightAt); else r.i = a.i / b.i; }
        else if (b.i < 0) fail(rightAt);
        else { // square-and-multiply, so a huge exponent does not loop for ages
            
            // Source: Wikipedia "Exponentiation by squaring" (https://en.wikipedia.org/wiki/Exponentiation_by_squaring).
            // I used the square-and-multiply idea so a big exponent needs only about log2(e) steps.
            long long x = a.i, e = b.i;
            r.i = 1;
            while (e) { if (e & 1) r.i *= x; e >>= 1; if (e) x *= x; }
        }
        return r;
    }
    double x = dv(a), y = dv(b);
    r.isf = 1;
    if (op == '/' && y == 0) { fail(rightAt); return r; }
    r.d = op == '+' ? x + y : op == '-' ? x - y : op == '*' ? x * y : op == '/' ? x / y : pow(x, y);
    if (!isfinite(r.d)) fail(opAt);
    return r;
}

static Val expr(void);

// AI (Claude): prompt "how does a tokenizer keep track of the position of a character in a whole file, not just the line and why do parsers report errors at a certain specifc position" Link: https://claude.ai/share/05bc7a50-f199-454f-9901-3579e464504a
// I used the explanation of why each error is reported where it is.
// The error position is always p at the moment we cannot continue, i.e. the start
// of the token we were looking at (after skipping spaces). That is the invalid
// character, the second operator, the ')' of "()", or the '\n' / end of file when
// an operand or ')' is missing.
static Val primary(void) {
    Val v = {0, 0, 0};
    skip();
    if (s[p] == '(') {
        p++;
        v = expr();
        skip();
        if (!err) { if (s[p] == ')') p++; else fail(p); }
    } else if (isdigit((unsigned char)s[p])) {
        long q = p;
        while (isdigit((unsigned char)s[p])) v.i = v.i * 10 + (s[p++] - '0');
        if (s[p] == '.') {
            if (!isdigit((unsigned char)s[p + 1])) fail(p); // "5." is invalid, error at the '.'
            else {
                p++;
                while (isdigit((unsigned char)s[p])) p++;

                // Source: cppreference strtod page (https://cppreference.com/c/string/byte/strtod).
                //   I used it to learn that strtod also reads an exponent like e3, which is why the string is ended early.
                char c = s[p]; // temporarily end the string so strtod cannot read an exponent
                s[p] = 0;
                v.d = strtod(s + q, NULL);
                s[p] = c;
                v.isf = 1;
            }
        } // anything left over such as the 'e' in 1.5e3 is reported by the caller at that character
    } else fail(p);
    return v;
}

// AI (Claude): prompt "why is ** right associative but + and - are left associative and how do i do that in a parser?" Link: https://claude.ai/share/05bc7a50-f199-454f-9901-3579e464504a
// I used the idea that calling power() again for the right side makes ** group from the right.
// Source: Wikipedia "Common operator notation" (https://en.wikipedia.org/wiki/Common_operator_notation): exponentiation is normally right-associative.
// I used this to decide that ** groups from the right.
// power calls itself for the exponent, so 2 ** 3 ** 2 groups as 2 ** (3 ** 2):
// that is what makes ** right-associative. It sits below term, so it binds tighter than * and /.
static Val power(void) {
    Val a = primary();
    skip();
    if (err || s[p] != '*' || s[p + 1] != '*') return a;
    long at = p;
    p += 2;
    skip();
    long rp = p;
    Val b = power();
    if (err) return a;
    return calc('p', a, b, at, rp);
}

// AI (Claude): prompt "what is recursive descent parser and how it make * and / happen before + and -" Link: https://claude.ai/share/05bc7a50-f199-454f-9901-3579e464504a
// I used the idea of one function per precedence level, with loops for left-to-right grouping.
// Source: Wikipedia "Operator associativity" (https://en.wikipedia.org/wiki/Operator_associativity) and Crafting Interpreters "Parsing Expressions" (https://craftinginterpreters.com/parsing-expressions.html).
// I used them for the loops that make + - * / group from the left, so 8 - 3 - 2 is (8 - 3) - 2.
// term and expr are loops, not recursion, so 8 - 3 - 2 is (8 - 3) - 2 (left-associative).
// expr calls term and term calls power, so * and / bind tighter than + and -.
static Val term(void) {
    Val a = power();
    for (skip(); !err && (s[p] == '*' || s[p] == '/'); skip()) {
        char op = s[p];
        long at = p++;
        skip();
        long rp = p;
        Val b = power();
        if (err) break;
        a = calc(op, a, b, at, rp);
    }
    return a;
}

static Val expr(void) {
    Val a = term();
    for (skip(); !err && (s[p] == '+' || s[p] == '-'); skip()) {
        char op = s[p];
        long at = p++;
        skip();
        long rp = p;
        Val b = term();
        if (err) break;
        a = calc(op, a, b, at, rp);
    }
    return a;
}

// Evaluates the file in, writes outdir/<base>_<name>_<lastname>_<id>.txt. Returns 0 on success.
static int run(const char *in, const char *outdir, const char *base) {
    FILE *f = fopen(in, "rb");
    if (!f) { fprintf(stderr, "calc: cannot open %s\n", in); return 1; }

    // AI (Claude): prompt "how can i read a whole file into memory in c language" Link: https://claude.ai/share/05bc7a50-f199-454f-9901-3579e464504a
    // I used the idea of loading the whole file at once; I wrote the explanation in my own words.
    // Source: cppreference fseek and rewind pages (https://cppreference.com/c/io/fseek).
    // I used the way of finding the file size with fseek to the end and ftell, then rewind and one fread, so the whole file is loaded once.
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    s = malloc(len + 1);
    if (!s) { fprintf(stderr, "out of memory\n"); exit(1); }
    s[fread(s, 1, len, f)] = 0;
    fclose(f);

    p = 0; err = 0; olen = 0;
    while (!err && s[p]) {
        skip();
        if (s[p] == '\n') { p++; continue; } // blank line (or the newline that ended the last expression)
        if (s[p] == '#') { while (s[p] && s[p] != '\n') p++; continue; } // only a FIRST non-blank '#' is a comment
        if (!s[p]) break;
        Val v = expr();
        skip();
        if (!err && s[p] && s[p] != '\n') fail(p); // leftover token: extra ')' or invalid character
        if (err) break;
        char t[64];
        
    // Source: Linux man page printf(3) (https://man7.org/linux/man-pages/man3/printf.3.html).
    //   I used it for %.15g: with %g the precision is the number of significant digits and trailing zeros are dropped, so 5.0 prints as 5.
        int n = v.isf ? snprintf(t, sizeof t, "%.15g\n", v.d) : snprintf(t, sizeof t, "%lld\n", v.i);
        add(t, n);
    }
    if (err) { // no partial results: throw away what we had and keep only the error
        char t[64];
        olen = 0;
        add(t, snprintf(t, sizeof t, "ERROR:%ld\n", err));
    }
    free(s);

    int rc = 0;
    char path[4096];

    // Source: Linux man page mkdir(2) (https://man7.org/linux/man-pages/man2/mkdir.2.html).
    // I used it to see that mkdir fails with EEXIST when the folder already exists, and that this case is fine.
    if (mkdir(outdir, 0777) != 0 && errno != EEXIST) rc = 1;
    snprintf(path, sizeof path, "%s/%s_%s_%s_%s.txt", outdir, base, NAME, LAST, ID);
    FILE *o = rc ? NULL : fopen(path, "wb");
    if (!o || fwrite(out, 1, olen, o) != olen) rc = 1;
    if (o && fclose(o) != 0) rc = 1;
    if (rc) fprintf(stderr, "calc: cannot write %s\n", outdir);
    free(out);
    out = NULL; olen = ocap = 0;
    return rc;
}

// Copies the last part of path into b, without trailing '/', and without ".txt" when cut is set.
static void baseName(const char *path, char *b, int cut) {
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;
    size_t st = n;
    while (st > 0 && path[st - 1] != '/') st--;
    snprintf(b, 512, "%.*s", (int)(n - st), path + st);
    n = strlen(b);
    if (cut && n > 4 && !strcmp(b + n - 4, ".txt")) b[n - 4] = 0;
}

int main(int argc, char **argv) {
    const char *dir = NULL, *outdir = NULL, *in = NULL;
    for (int i = 1; i < argc; i++) {
        if ((!strcmp(argv[i], "-d") || !strcmp(argv[i], "--dir")) && i + 1 < argc) dir = argv[++i];
        else if ((!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output-dir")) && i + 1 < argc) outdir = argv[++i];
        else if (argv[i][0] != '-' && !in) in = argv[i];
        else { fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n"); return 1; }
    }
    if (!dir == !in) { fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n"); return 1; }

    char base[512], def[1024], name[512];
    baseName(dir ? dir : in, base, !dir);
    if (!outdir) { snprintf(def, sizeof def, "%s_%s_%s", base, NAME, ID); outdir = def; }
    if (in) return run(in, outdir, base);

    // Source: Linux man pages opendir(3) (https://man7.org/linux/man-pages/man3/opendir.3.html), readdir(3) (https://man7.org/linux/man-pages/man3/readdir.3.html) and stat(2) (https://man7.org/linux/man-pages/man2/stat.2.html).
    //   I used them to loop over the files in a folder and to check with S_ISREG that an entry is a normal file.
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "calc: cannot open folder %s\n", dir); return 1; }
    int rc = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        char path[4096];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (n > 4 && !strcmp(e->d_name + n - 4, ".txt") && stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            baseName(e->d_name, name, 1);
            rc |= run(path, outdir, name);
        }
    }
    closedir(d);
    return rc;
}
