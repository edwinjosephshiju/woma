#include "Python.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <ctype.h>
#include "llama.h"

#ifdef _WIN32
#include <windows.h>
#endif

// ----------------------------------------------------------------------------
// Phase 3: Hash Generation & Verification
// ----------------------------------------------------------------------------
int compute_sha256(const char *path, char *output_hash) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), f)) != 0) {
        SHA256_Update(&ctx, buffer, bytes);
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);
    fclose(f);

    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output_hash + (i * 2), "%02x", hash[i]);
    }
    output_hash[64] = '\0';
    return 0;
}

char* read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long fsize = ftell(f);
    if (fsize < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *string = malloc(fsize + 1);
    if (!string) { fclose(f); return NULL; }
    size_t read = fread(string, 1, (size_t)fsize, f);
    fclose(f);
    if (read != (size_t)fsize) {
        free(string);
        return NULL;
    }

    string[fsize] = '\0';
    return string;
}

// ----------------------------------------------------------------------------
// Phase 4: Embedded AI Inference (llama.cpp)
// ----------------------------------------------------------------------------
int is_chatbot_prompt(const char* code) {
    char lower[256] = {0};
    strncpy(lower, code, 255);
    for(int i = 0; lower[i]; i++){
        lower[i] = tolower((unsigned char)lower[i]);
    }

    if (strstr(lower, "write a script") ||
        strstr(lower, "create a script") ||
        strstr(lower, "make a python") ||
        strstr(lower, "write a program") ||
        strstr(lower, "create a program") ||
        strstr(lower, "can you write") ||
        strstr(lower, "generate a script") ||
        strstr(lower, "make a script")) {
        return 1;
    }
    return 0;
}

static int contains_rejected_error(const char *code) {
    if (!code) return 0;
    const char *target = "woma_compiler_error_rejected";
    int t_len = strlen(target);
    int c_len = strlen(code);
    if (c_len < t_len) return 0;
    for (int i = 0; i <= c_len - t_len; i++) {
        int match = 1;
        for (int j = 0; j < t_len; j++) {
            if (tolower((unsigned char)code[i+j]) != target[j]) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

static struct llama_model *global_model = NULL;
static struct llama_context *global_ctx = NULL;
static const struct llama_vocab *global_vocab = NULL;
static struct llama_context_params global_ctx_params;

static void get_woma_ai_context(void) {
    if (global_model) return;

    llama_backend_init();

    struct llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 99; // Offload to RTX 4050 (CUDA) if LLAMA_CUBLAS=1

    char tmp_model_path[1024];
#ifdef _WIN32
    GetModuleFileNameA(NULL, tmp_model_path, sizeof(tmp_model_path));
    char *last_slash = strrchr(tmp_model_path, '\\');
    if (last_slash) {
        strcpy(last_slash + 1, "model.gguf");
    } else {
        strcpy(tmp_model_path, "model.gguf");
    }
#else
    strcpy(tmp_model_path, "/usr/share/woma/model.gguf");
#endif

    struct stat st;
    if (stat(tmp_model_path, &st) != 0) {
        fprintf(stderr, "FATAL: AI model not found at %s. Please ensure Woma is properly installed.\n", tmp_model_path);
        exit(1);
    }

    global_model = llama_model_load_from_file(tmp_model_path, model_params);
    if (!global_model) {
        fprintf(stderr, "FATAL: Failed to load AI model\n");
        exit(1);
    }
    printf("[*] Loaded Qwen2.5-Coder Successfully...\n");

    global_ctx_params = llama_context_default_params();
    global_ctx_params.n_ctx = 4096; // 4K context for code translation
    global_vocab = llama_model_get_vocab(global_model);
    global_ctx = llama_init_from_model(global_model, global_ctx_params);
}

char* run_llama_transpilation(const char *woma_code, const char *memory_context, char **new_memory_context);

static char *(*original_readline)(FILE *, FILE *, const char *);

static char *fallback_readline(FILE *sys_stdin, FILE *sys_stdout, const char *prompt) {
    if (sys_stdout && prompt) {
        fprintf(sys_stdout, "%s", prompt);
        fflush(sys_stdout);
    }
    char buf[4096];
    if (!fgets(buf, sizeof(buf), sys_stdin)) {
        return NULL;
    }
    char *res = PyMem_RawMalloc(strlen(buf) + 1);
    if (res) strcpy(res, buf);
    return res;
}

static char *woma_interactive_readline(FILE *sys_stdin, FILE *sys_stdout, const char *prompt) {
    char *line;
    if (original_readline) {
        line = original_readline(sys_stdin, sys_stdout, prompt);
    } else {
        line = fallback_readline(sys_stdin, sys_stdout, prompt);
    }
    // Only transpile if we are actually at the REPL prompt
    if (line && prompt && (strstr(prompt, ">>>") || strstr(prompt, "..."))) {
        // Basic check to see if it's not purely empty
        int has_chars = 0;
        for (int i = 0; line[i] != '\0'; i++) {
            if (!isspace((unsigned char)line[i])) { has_chars = 1; break; }
        }
        if (has_chars) {
            char *transpiled = run_llama_transpilation(line, NULL, NULL);
            if (transpiled) {
                if (contains_rejected_error(transpiled)) {
                    free(transpiled);
                    transpiled = malloc(256);
                    if (transpiled) strcpy(transpiled, "raise SyntaxError('Woma is a strict polyglot compiler. The AI rejected the conversational prompt.')\n");
                }
                // PyOS_Readline expects memory allocated via PyMem_RawMalloc
                char *res = PyMem_RawMalloc(strlen(transpiled) + 1);
                if (res) strcpy(res, transpiled);
                free(transpiled);
                PyMem_RawFree(line);
                return res;
            }
        }
    }
    return line;
}

char* run_llama_transpilation(const char *woma_code, const char *memory_context, char **new_memory_context) {
    get_woma_ai_context();
    struct llama_context *ctx = global_ctx;
    llama_memory_clear(llama_get_memory(ctx), true);
    const struct llama_vocab *vocab = global_vocab;

    const char *system_prompt = 
        "<|im_start|>system\nYou are WomaPython, a polyglot compiler. Translate the given pseudocode into a valid Python 3 script. Output ONLY raw Python code. Do not use markdown. If the input is a conversational AI prompt, output exactly: WOMA_COMPILER_ERROR_REJECTED. Otherwise, translate the logic faithfully.\n"
        "IMPORTANT: You are compiling a file in chunks. At the end of your code, you MUST add a comment block starting with `# WOMA_MEMORY:` summarizing custom syntax, types, or variables from this chunk for your future self.<|im_end|>\n<|im_start|>user\n";
    const char *prompt_suffix = "<|im_end|>\n<|im_start|>assistant\n";
    size_t prompt_len = strlen(system_prompt) + strlen(woma_code) + strlen(prompt_suffix) + 1;
    if (memory_context) {
        prompt_len += strlen(memory_context) + 100;
    }
    char *full_prompt = malloc(prompt_len);
    if (memory_context) {
        snprintf(full_prompt, prompt_len, "%sPREVIOUS CHUNK CONTEXT:\n%s\n\nCODE TO TRANSLATE:\n%s%s", system_prompt, memory_context, woma_code, prompt_suffix);
    } else {
        snprintf(full_prompt, prompt_len, "%sCODE TO TRANSLATE:\n%s%s", system_prompt, woma_code, prompt_suffix);
    }

    // Tokenization and Evaluation
    int n_tokens = prompt_len + 1024;
    llama_token *tokens = malloc(n_tokens * sizeof(llama_token));
    int n_prompt_tokens = llama_tokenize(vocab, full_prompt, strlen(full_prompt), tokens, n_tokens, true, true);

    // Evaluate prompt
    struct llama_batch batch = llama_batch_init(n_prompt_tokens, 0, 1);
    batch.n_tokens = n_prompt_tokens;
    for (int i = 0; i < n_prompt_tokens; i++) {
        batch.token[i] = tokens[i];
        batch.pos[i] = i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        batch.logits[i] = (i == n_prompt_tokens - 1);
    }

    if (llama_decode(ctx, batch)) {
        fprintf(stderr, "FATAL: Failed to decode prompt\n");
        exit(1);
    }

    // Allocate buffer for output Python code
    size_t out_cap = 8192;
    char *out_code = malloc(out_cap);
    out_code[0] = '\0';
    size_t out_len = 0;

    // Token generation loop
    uint32_t n_cur = n_prompt_tokens;
    while (n_cur <= global_ctx_params.n_ctx) {
        float *logits = llama_get_logits_ith(ctx, -1);
        int n_vocab = llama_vocab_n_tokens(vocab);

        llama_token new_token_id = 0;
        float max_logit = logits[0];
        for (int i = 1; i < n_vocab; i++) {
            if (logits[i] > max_logit) { max_logit = logits[i]; new_token_id = i; }
        }

        if (llama_vocab_is_eog(vocab, new_token_id)) {
            break; // Finished generating (reached EOS or <|im_end|>)
        }

        // Append token to string
        char buf[256];
        int piece_len = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf) - 1, 0, true);
        if (piece_len > 0) {
            buf[piece_len] = '\0';
            strncat(out_code, buf, out_cap - out_len - 1);
            out_len += piece_len;
        }

        batch.n_tokens = 1;
        batch.token[0] = new_token_id;
        batch.pos[0] = n_cur;
        batch.n_seq_id[0] = 1;
        batch.seq_id[0][0] = 0;
        batch.logits[0] = true;

        llama_decode(ctx, batch);
        n_cur++;
    }

    // Cleanup
    llama_batch_free(batch);
    free(full_prompt);
    free(tokens);

    // Post-process to remove markdown code blocks
    char *start = strstr(out_code, "```python");
    if (start) {
        start += 9;
        while (*start == '\n' || *start == '\r') start++;
        char *end = strstr(start, "```");
        if (end) {
            *end = '\0';
        }
        memmove(out_code, start, strlen(start) + 1);
    } else {
        start = strstr(out_code, "```");
        if (start) {
            start += 3;
            while (*start == '\n' || *start == '\r') start++;
            char *end = strstr(start, "```");
            if (end) {
                *end = '\0';
            }
            memmove(out_code, start, strlen(start) + 1);
        }
    }

    char *memory_ptr = strstr(out_code, "# WOMA_MEMORY:");
    if (memory_ptr) {
        if (new_memory_context) {
            size_t mem_len = strlen(memory_ptr + 14);
            *new_memory_context = malloc(mem_len + 1);
            if (*new_memory_context) strcpy(*new_memory_context, memory_ptr + 14);
        }
        *memory_ptr = '\0';
    } else {
        if (new_memory_context) *new_memory_context = NULL;
    }

    size_t final_len = strlen(out_code);
    if (final_len > 0 && out_code[final_len - 1] != '\n') {
        if (final_len + 1 < out_cap) {
            out_code[final_len] = '\n';
            out_code[final_len + 1] = '\0';
        }
    }
    return out_code;
}

// ----------------------------------------------------------------------------
// Phase 5: Dynamic Dependency Injection
// ----------------------------------------------------------------------------
void inject_dependencies(const char *py_code) {
    // Run an embedded Python snippet that uses the AST module to parse the code,
    // find imports, check if they exist, and pip install if missing.
    const char *resolver_script =
        "import ast\n"
        "import sys\n"
        "import os\n"
        "import importlib.util\n"
        "sys.path.insert(0, os.getcwd())\n"
        "code = sys.argv[-1]\n"
        "tree = ast.parse(code)\n"
        "imports = set()\n"
        "for node in ast.walk(tree):\n"
        "    if isinstance(node, ast.Import):\n"
        "        for alias in node.names:\n"
        "            imports.add(alias.name.split('.')[0])\n"
        "    elif isinstance(node, ast.ImportFrom):\n"
        "        if node.module:\n"
        "            imports.add(node.module.split('.')[0])\n"
        "\n"
        "for mod in imports:\n"
        "    if not importlib.util.find_spec(mod):\n"
        "        print(f'[*] WomaPython: Auto-installing missing dependency: {mod}')\n"
        "        os.system(f'env -u PYTHONHOME pip install -q --break-system-packages -t . {mod}')\n";

    // We pass the code string as the last argument in sys.argv temporarily
    PyObject *sys_module = PyImport_ImportModule("sys");
    PyObject *sys_argv = PyObject_GetAttrString(sys_module, "argv");
    PyObject *py_code_str = PyUnicode_FromString(py_code);
    PyList_Append(sys_argv, py_code_str);

    PyRun_SimpleString(resolver_script);

    // Remove the temporary code string from sys.argv to clean up
    // (We use a simple Python command for cleanup here for brevity)
    PyRun_SimpleString("sys.argv.pop()");

    Py_DECREF(py_code_str);
    Py_DECREF(sys_argv);
    Py_DECREF(sys_module);
}

static void silent_log_callback(enum ggml_log_level level, const char * text, void * user_data) {
    (void)level;
    (void)text;
    (void)user_data;
}


static PyObject* set_repl_hook(PyObject *self, PyObject *args) {
    if (PyOS_ReadlineFunctionPointer != woma_interactive_readline) {
        original_readline = PyOS_ReadlineFunctionPointer;
        PyOS_ReadlineFunctionPointer = woma_interactive_readline;
    }
    Py_RETURN_NONE;
}

static PyMethodDef WomaMethods[] = {
    {"set_repl_hook", set_repl_hook, METH_NOARGS, "Set the AI REPL hook."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef womamodule = {
    PyModuleDef_HEAD_INIT, "_woma", NULL, -1, WomaMethods
};

PyMODINIT_FUNC PyInit__woma(void) {
    return PyModule_Create(&womamodule);
}

// ----------------------------------------------------------------------------
// Main Entrypoint

// ----------------------------------------------------------------------------
int main(int argc, char **argv) {
    int ai_debug = 0;
    int new_argc = 0;
    char **new_argv = malloc((argc + 1) * sizeof(char *));
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--AI_debug") == 0) {
            ai_debug = 1;
        } else {
            new_argv[new_argc++] = argv[i];
        }
    }
    new_argv[new_argc] = NULL;

    if (!ai_debug) {
        llama_log_set(silent_log_callback, NULL);
    }

    if (new_argc < 2) {
        PyImport_AppendInittab("_woma", PyInit__woma);

        char *interactive_args[5];
        interactive_args[0] = new_argv[0];
        interactive_args[1] = "-i";
        interactive_args[2] = "-c";
        interactive_args[3] = "import sys\n"
                              "sys.stderr.write(f'WomaPython 1.0.0 (AI-Enhanced Runtime) on {sys.platform}\\n"
                              "Type \"help\", \"copyright\", \"credits\" or \"license\" for more information.\\n')\n"
                              "try:\n"
                              "    import readline\n"
                              "except ImportError:\n"
                              "    pass\n"
                              "import _woma\n"
                              "_woma.set_repl_hook()\n";
        interactive_args[4] = NULL;

        int ret = Py_BytesMain(4, interactive_args);
        free(new_argv);
        return ret;
    }
    const char *input_file = new_argv[1];
    size_t len = strlen(input_file);

    if (len > 5 && strcmp(input_file + len - 5, ".woma") == 0) {
        // Phase 3: Check cache
        char hash[65];
        if (compute_sha256(input_file, hash) != 0) {
            fprintf(stderr, "Error: Could not read .woma file\n");
            return 1;
        }

        char py_file[1024];
        snprintf(py_file, sizeof(py_file), "%.*s.py", (int)(len - 5), input_file);

        int cache_hit = 0;
        FILE *pf = fopen(py_file, "r");
        if (pf) {
            char line[256];
            if (fgets(line, sizeof(line), pf)) {
                char expected_header[128];
                snprintf(expected_header, sizeof(expected_header), "# WOMA_HASH: %s\n", hash);
                if (strcmp(line, expected_header) == 0) {
                    cache_hit = 1;
                }
            }
            fclose(pf);
        }

        char *python_code = NULL;

        if (cache_hit) {
            // Cache Hit: Read the existing Python code
            python_code = read_file(py_file);
        } else {
            // Cache Miss: Phase 4
            printf("[*] WomaPython: Cache miss. Initializing AI transpiler...\n");
            char *woma_code = read_file(input_file);

            python_code = malloc(1);
            python_code[0] = '\0';
            size_t py_code_len = 0;
            size_t py_code_cap = 1;

            char *curr = woma_code;
            size_t remaining = woma_code ? strlen(woma_code) : 0;
            size_t MAX_CHUNK_SIZE = 8000;

            char *memory_context = NULL;

            while (remaining > 0) {
                size_t chunk_len = remaining;
                if (chunk_len > MAX_CHUNK_SIZE) {
                    chunk_len = MAX_CHUNK_SIZE;
                    char *last_double_nl = NULL;
                    for (size_t i = chunk_len; i > 0; i--) {
                        if (curr[i] == '\n' && curr[i-1] == '\n') {
                            last_double_nl = curr + i;
                            break;
                        }
                    }
                    if (last_double_nl) {
                        chunk_len = last_double_nl - curr;
                    } else {
                        char *last_nl = NULL;
                        for (size_t i = chunk_len; i > 0; i--) {
                            if (curr[i] == '\n') {
                                last_nl = curr + i;
                                break;
                            }
                        }
                        if (last_nl) chunk_len = last_nl - curr;
                    }
                }

                char *chunk = malloc(chunk_len + 1);
                strncpy(chunk, curr, chunk_len);
                chunk[chunk_len] = '\0';

                if (is_chatbot_prompt(chunk)) {
                    fprintf(stderr, "SyntaxError: Woma is a strict polyglot compiler. Conversational prompts are rejected.\n");
                    free(chunk);
                    free(python_code);
                    free(woma_code);
                    if (memory_context) free(memory_context);
                    return 1;
                }

                char *new_memory_context = NULL;
                char *transpiled = run_llama_transpilation(chunk, memory_context, &new_memory_context);
                free(chunk);

                if (memory_context) free(memory_context);
                memory_context = new_memory_context;

                if (contains_rejected_error(transpiled)) {
                    fprintf(stderr, "SyntaxError: Woma is a strict polyglot compiler. The AI rejected the conversational prompt.\n");
                    free(transpiled);
                    free(python_code);
                    free(woma_code);
                    if (memory_context) free(memory_context);
                    return 1;
                }

                size_t t_len = transpiled ? strlen(transpiled) : 0;
                if (t_len > 0) {
                    if (py_code_len + t_len + 2 > py_code_cap) {
                        py_code_cap = (py_code_len + t_len) * 2 + 2;
                        python_code = realloc(python_code, py_code_cap);
                    }
                    strcpy(python_code + py_code_len, transpiled);
                    py_code_len += t_len;
                    if (python_code[py_code_len - 1] != '\n') {
                        python_code[py_code_len] = '\n';
                        python_code[py_code_len + 1] = '\0';
                        py_code_len++;
                    }
                }
                free(transpiled);

                curr += chunk_len;
                remaining -= chunk_len;
            }
            if (memory_context) free(memory_context);
            if (woma_code) free(woma_code);

            // File Write: Save the transpiled code
            FILE *out = fopen(py_file, "w");
            if (out) {
                fprintf(out, "# WOMA_HASH: %s\n", hash);
                fprintf(out, "%s\n", python_code);
                fclose(out);
            }
        }

        // Initialize CPython interpreter explicitly before running dynamic dependency injection
        Py_Initialize();

        // Phase 5: Dynamic Dependency Injection
        if (python_code) {
            inject_dependencies(python_code);
        }

        // Phase 6: Final Execution & Memory Safety
        PyRun_SimpleString("import sys, os\nsys.path.insert(0, os.getcwd())\n");
        
        // Execute the python_code string directly to avoid FILE* cross-CRT issues on Windows
        PyObject *m, *d, *v;
        m = PyImport_AddModule("__main__");
        if (m == NULL) {
            PyErr_Print();
        } else {
            d = PyModule_GetDict(m);
            v = PyRun_String(python_code, Py_file_input, d, d);
            if (v == NULL) {
                PyErr_Print();
            } else {
                Py_DECREF(v);
            }
        }

        // Cleanup C memory and Python runtime
        free(python_code);
        Py_Finalize();
        free(new_argv);
        return 0;
    }

    // Normal Python behavior if not a .woma file
    int ret = Py_BytesMain(new_argc, new_argv);
    free(new_argv);
    return ret;
}
