#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>
#include <stdexcept>

using namespace std;

enum OpType { LD, ADD, SUB, MUL, DIV, NONE };

struct Instruction {
    string raw;
    OpType op;
    string dest, src1, src2;

    int cycle_issue         = -1;
    int cycle_execute_start = -1;
    int cycle_execute_end   = -1;
    int cycle_write         = -1;
    bool is_done            = false;
    string assigned_station = "";
};

struct ReservationStation {
    string name;
    OpType type;
    bool busy      = false;
    OpType op      = NONE;
    float vj = 0, vk = 0;
    string qj = "", qk = "";
    int time_left  = 0;
    int instr_index = -1;
};

struct RegisterStatus {
    string name;
    float value = 0;
    string qi   = "";
};

// ---- Latências ----
const int LAT_LD      = 2;
const int LAT_ADD_SUB = 2;
const int LAT_MUL     = 10;
const int LAT_DIV     = 40;

vector<Instruction>        instr_queue;
vector<ReservationStation> stations;
vector<RegisterStatus>     registers;
int    current_cycle = 1;
size_t issue_ptr     = 0;

// -------------------------------------------------------
// Utilidades
// -------------------------------------------------------
string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == string::npos) ? "" : s.substr(a, b - a + 1);
}

string to_upper(string s) {
    for (auto& c : s) c = toupper(c);
    return s;
}

string op_str(OpType op) {
    switch (op) {
        case LD:  return "L.D";
        case ADD: return "ADD.D";
        case SUB: return "SUB.D";
        case MUL: return "MUL.D";
        case DIV: return "DIV.D";
        default:  return "";
    }
}

int latency(OpType op) {
    if (op == LD)               return LAT_LD;
    if (op == ADD || op == SUB) return LAT_ADD_SUB;
    if (op == MUL)              return LAT_MUL;
    if (op == DIV)              return LAT_DIV;
    return 1;
}

bool station_accepts(const ReservationStation& st, OpType op) {
    if (op == LD)               return st.type == LD;
    if (op == ADD || op == SUB) return st.type == ADD;
    if (op == MUL || op == DIV) return st.type == MUL;
    return false;
}

RegisterStatus* find_reg(const string& name) {
    for (auto& r : registers)
        if (r.name == name) return &r;
    return nullptr;
}

// -------------------------------------------------------
// PARSER DE ASSEMBLY
// Suporta: L.D / LD, ADD.D, SUB.D, MUL.D, DIV.D
// Formato: OP DEST, SRC1, SRC2   ou   L.D DEST, OFFSET(BASE)
// -------------------------------------------------------
OpType parse_op(const string& tok) {
    string u = to_upper(tok);
    if (u == "L.D"   || u == "LD")    return LD;
    if (u == "ADD.D" || u == "ADD")   return ADD;
    if (u == "SUB.D" || u == "SUB")   return SUB;
    if (u == "MUL.D" || u == "MUL")   return MUL;
    if (u == "DIV.D" || u == "DIV")   return DIV;
    throw runtime_error("Operacao desconhecida: " + tok);
}

// Remove vírgulas e espaços de um token
string clean_token(const string& s) {
    string r;
    for (char c : s)
        if (c != ',' && c != ' ' && c != '\t') r += c;
    return r;
}

Instruction parse_line(const string& line) {
    Instruction inst;
    inst.raw = trim(line);

    istringstream ss(inst.raw);
    string op_tok;
    ss >> op_tok;
    inst.op = parse_op(op_tok);

    string dest_tok, src1_tok, src2_tok;

    if (inst.op == LD) {
        // Formato: L.D F6, 34(R2)  ->  dest=F6, src1=R2, src2=""
        ss >> dest_tok >> src1_tok;
        dest_tok = clean_token(dest_tok);
        src1_tok = clean_token(src1_tok);

        // Extrai o registrador base do offset(REG)
        size_t pa = src1_tok.find('(');
        size_t pb = src1_tok.find(')');
        if (pa != string::npos && pb != string::npos)
            src1_tok = src1_tok.substr(pa + 1, pb - pa - 1);

        inst.dest = dest_tok;
        inst.src1 = src1_tok;
        inst.src2 = "";
    } else {
        // Formato: ADD.D F0, F2, F4
        ss >> dest_tok >> src1_tok >> src2_tok;
        inst.dest = clean_token(dest_tok);
        inst.src1 = clean_token(src1_tok);
        inst.src2 = clean_token(src2_tok);
    }

    return inst;
}

bool load_program(const string& filename) {
    ifstream f(filename);
    if (!f.is_open()) {
        cerr << "  [ERRO] Nao foi possivel abrir o arquivo: " << filename << "\n";
        return false;
    }

    string line;
    int line_num = 0;
    while (getline(f, line)) {
        line_num++;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue; // comentários
        try {
            instr_queue.push_back(parse_line(line));
        } catch (const exception& e) {
            cerr << "  [ERRO] Linha " << line_num << ": " << e.what() << "\n";
            return false;
        }
    }

    if (instr_queue.empty()) {
        cerr << "  [ERRO] Arquivo vazio ou sem instrucoes validas.\n";
        return false;
    }
    return true;
}

// -------------------------------------------------------
// CONFIGURAÇÃO DAS ESTAÇÕES VIA TERMINAL
// -------------------------------------------------------
void config_stations() {
    cout << "\n  Configure as estacoes de reserva:\n";

    struct StationType { string prefix; OpType type; string label; };
    vector<StationType> types = {
        {"Load", LD,  "Load/Store (LD)"},
        {"Add",  ADD, "Add/Sub (ADD.D, SUB.D)"},
        {"Mult", MUL, "Mult/Div (MUL.D, DIV.D)"},
    };

    for (auto& t : types) {
        int n = 0;
        while (true) {
            cout << "  Quantas estacoes de " << t.label << "? ";
            if (cin >> n && n > 0 && n <= 10) break;
            cin.clear(); cin.ignore(1000, '\n');
            cout << "  Digite um numero entre 1 e 10.\n";
        }
        for (int i = 1; i <= n; i++) {
            ReservationStation st;
            st.name = t.prefix + to_string(i);
            st.type = t.type;
            stations.push_back(st);
        }
    }
    cin.ignore(1000, '\n'); // limpa buffer para o getline do Enter
}

// -------------------------------------------------------
// INICIALIZAÇÃO DOS REGISTRADORES
// Registradores mencionados nas instruções, inicializados com valor = índice * 2
// -------------------------------------------------------
void init_registers() {
    auto ensure_reg = [&](const string& name) {
        if (name.empty()) return;
        if (name[0] != 'F' && name[0] != 'f') return; // ignora R0, R2 etc.
        for (auto& r : registers)
            if (r.name == name) return;
        int idx = registers.size();
        registers.push_back({name, (float)(idx * 2), ""});
    };

    for (auto& inst : instr_queue) {
        ensure_reg(inst.dest);
        ensure_reg(inst.src1);
        ensure_reg(inst.src2);
    }
}

// -------------------------------------------------------
// IMPRESSÃO DO ESTADO
// -------------------------------------------------------
void print_state() {
    cout << "\n============================================================\n";
    cout << "  CICLO " << current_cycle << "\n";
    cout << "============================================================\n";

    cout << "\n  [INSTRUCOES]\n";
    cout << "  " << left
         << setw(24) << "Instrucao"
         << setw(7)  << "Issue"
         << setw(14) << "Execute"
         << setw(7)  << "Write\n";
    cout << "  " << string(52, '-') << "\n";
    for (const auto& i : instr_queue) {
        string exec = "";
        if (i.cycle_execute_start > 0) {
            exec = to_string(i.cycle_execute_start);
            exec += i.cycle_execute_end > 0 ? " - " + to_string(i.cycle_execute_end) : " - ...";
        }
        cout << "  " << left
             << setw(24) << i.raw
             << setw(7)  << (i.cycle_issue > 0    ? to_string(i.cycle_issue) : "-")
             << setw(14) << (exec.empty()          ? "-" : exec)
             << setw(7)  << (i.cycle_write > 0     ? to_string(i.cycle_write) : "-")
             << "\n";
    }

    cout << "\n  [ESTACOES DE RESERVA]\n";
    cout << "  " << left
         << setw(8)  << "Nome"
         << setw(6)  << "Busy"
         << setw(8)  << "Op"
         << setw(8)  << "Vj"
         << setw(8)  << "Vk"
         << setw(8)  << "Qj"
         << setw(8)  << "Qk"
         << setw(5)  << "Lat\n";
    cout << "  " << string(59, '-') << "\n";
    for (const auto& st : stations) {
        cout << "  " << left
             << setw(8) << st.name
             << setw(6) << (st.busy ? "Sim" : "Nao")
             << setw(8) << (st.busy ? op_str(st.op) : "")
             << setw(8) << (st.busy && st.qj.empty() ? to_string((int)st.vj) : "")
             << setw(8) << (st.busy && st.qk.empty() ? to_string((int)st.vk) : "")
             << setw(8) << (st.busy ? st.qj : "")
             << setw(8) << (st.busy ? st.qk : "")
             << setw(5) << (st.busy ? to_string(st.time_left) : "")
             << "\n";
    }

    cout << "\n  [REGISTRADORES]\n  ";
    for (const auto& r : registers) {
        if (r.qi.empty()) cout << r.name << "=" << (int)r.value << "  ";
        else              cout << r.name << "=[" << r.qi << "]  ";
    }
    cout << "\n";
}

// -------------------------------------------------------
// SIMULAÇÃO — um ciclo por chamada
// -------------------------------------------------------
void simulate_cycle() {
    // === ETAPA 1: ISSUE ===
    if (issue_ptr < instr_queue.size()) {
        Instruction& inst = instr_queue[issue_ptr];

        ReservationStation* free_st = nullptr;
        for (auto& st : stations)
            if (!st.busy && station_accepts(st, inst.op)) { free_st = &st; break; }

        if (free_st) {
            free_st->busy        = true;
            free_st->op          = inst.op;
            free_st->instr_index = (int)issue_ptr;
            free_st->time_left   = latency(inst.op);
            free_st->vj = free_st->vk = 0;
            free_st->qj = free_st->qk = "";

            inst.cycle_issue      = current_cycle;
            inst.assigned_station = free_st->name;

            if (inst.op == LD) {
                // Load: endereço resolvido imediatamente
                free_st->qj = free_st->qk = "";
            } else {
                RegisterStatus* rj = find_reg(inst.src1);
                if (rj) { if (rj->qi.empty()) free_st->vj = rj->value; else free_st->qj = rj->qi; }
                RegisterStatus* rk = find_reg(inst.src2);
                if (rk) { if (rk->qi.empty()) free_st->vk = rk->value; else free_st->qk = rk->qi; }
            }

            // Renomeação de registrador
            RegisterStatus* rd = find_reg(inst.dest);
            if (rd) rd->qi = free_st->name;

            issue_ptr++;
        }
    }

    // === ETAPA 2: EXECUTE ===
    for (auto& st : stations) {
        if (!st.busy) continue;
        Instruction& inst = instr_queue[st.instr_index];
        if (!st.qj.empty() || !st.qk.empty()) continue;       // operandos pendentes
        if (inst.cycle_issue >= current_cycle) continue;       // não executa no mesmo ciclo do issue

        if (inst.cycle_execute_start == -1)
            inst.cycle_execute_start = current_cycle;

        if (inst.cycle_execute_end == -1) {
            st.time_left--;
            if (st.time_left == 0)
                inst.cycle_execute_end = current_cycle;
        }
    }

    // === ETAPA 3: WRITE RESULT (CDB — um por ciclo) ===
    for (auto& st : stations) {
        if (!st.busy) continue;
        Instruction& inst = instr_queue[st.instr_index];
        if (inst.cycle_execute_end <= 0) continue;
        if (inst.cycle_execute_end >= current_cycle) continue; // escreve no ciclo após terminar
        if (inst.cycle_write != -1) continue;

        float cdb_value    = 99.0f;
        string cdb_station = st.name;

        inst.cycle_write = current_cycle;
        inst.is_done     = true;

        // Broadcast no CDB
        for (auto& other : stations) {
            if (!other.busy || &other == &st) continue;
            if (other.qj == cdb_station) { other.vj = cdb_value; other.qj = ""; }
            if (other.qk == cdb_station) { other.vk = cdb_value; other.qk = ""; }
        }
        for (auto& r : registers) {
            if (r.qi == cdb_station) { r.value = cdb_value; r.qi = ""; }
        }

        st.busy        = false;
        st.op          = NONE;
        st.instr_index = -1;
        st.qj = st.qk  = "";
        st.vj = st.vk  = 0;
        st.time_left   = 0;

        break; // apenas um write por ciclo
    }
}

bool simulation_done() {
    for (const auto& i : instr_queue)
        if (!i.is_done) return false;
    return true;
}

// -------------------------------------------------------
int main(int argc, char* argv[]) {
    cout << "\n==========================================\n";
    cout << "  SIMULADOR DO ALGORITMO DE TOMASULO\n";
    cout << "==========================================\n";

    // --- Entrada: arquivo assembly ---
    string filename;
    if (argc >= 2) {
        filename = argv[1];
    } else {
        cout << "\n  Informe o caminho do arquivo assembly (.asm/.txt): ";
        cin >> filename;
        cin.ignore(1000, '\n');
    }

    if (!load_program(filename)) return 1;

    cout << "\n  Instrucoes carregadas:\n";
    for (size_t i = 0; i < instr_queue.size(); i++)
        cout << "  [" << i+1 << "] " << instr_queue[i].raw << "\n";

    // --- Entrada: estações de reserva ---
    config_stations();

    // --- Inicializa registradores ---
    init_registers();

    cout << "\n  Latencias: LD=" << LAT_LD << "  ADD/SUB=" << LAT_ADD_SUB
         << "  MUL=" << LAT_MUL << "  DIV=" << LAT_DIV << "\n";
    cout << "  Pressione ENTER para avancar cada ciclo.\n";

    // --- Loop de simulação ---
    while (!simulation_done() && current_cycle <= 200) {
        simulate_cycle();
        print_state();

        if (!simulation_done()) {
            cout << "\n  [Enter para ciclo " << current_cycle + 1 << "] ";
            cin.get();
        }
        current_cycle++;
    }

    cout << "\n==========================================\n";
    cout << "  Simulacao concluida no ciclo " << current_cycle - 1 << "\n";
    cout << "==========================================\n";
    cout << "\n  [RESUMO FINAL]\n";
    cout << "  " << left << setw(24) << "Instrucao"
         << setw(7) << "Issue" << setw(14) << "Execute" << setw(7) << "Write\n";
    cout << "  " << string(52, '-') << "\n";
    for (const auto& i : instr_queue) {
        string exec = to_string(i.cycle_execute_start) + " - " + to_string(i.cycle_execute_end);
        cout << "  " << left << setw(24) << i.raw
             << setw(7) << i.cycle_issue
             << setw(14) << exec
             << setw(7) << i.cycle_write << "\n";
    }

    return 0;
}