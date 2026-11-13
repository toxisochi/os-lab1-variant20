// Дочерняя программа 1.
#include <unistd.h>
#include <string>
#include <cerrno>
using namespace std;

bool write_all(int fd, const string& text)
{
    size_t offset = 0;
    while (offset < text.size())
    {
        ssize_t sent = write(fd, text.data() + offset, text.size() - offset);
        if (sent == -1)
        {
            if (errno == EINTR) continue;
            return false;
        }
        if (sent == 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

void print_error(const string& message)
{
    write_all(STDERR_FILENO, message + "\n");
}

// 1 — прочитана строка, 0 — конец ввода, -1 — ошибка.
int read_line(string& line)
{
    line.clear();
    char symbol;
    while (true)
    {
        ssize_t received = read(STDIN_FILENO, &symbol, 1);
        if (received == -1)
        {
            if (errno == EINTR) continue;
            return -1;
        }
        if (received == 0)
        {
            if (line.empty()) return 0;
            return 1;
        }
        if (symbol == '\n') return 1;
        line += symbol;
    }
}

// Переворачиваем символы UTF-8, сохраняя порядок байтов внутри символа.
bool send_reversed(string& line)
{
    string result;
    size_t end = line.size();
    while (end > 0)
    {
        size_t start = end - 1;
        while (start > 0)
        {
            unsigned char byte = static_cast<unsigned char>(line[start]);
            if (byte < 128 || byte >= 192) break;
            --start;
        }
        for (size_t i = start; i < end; ++i)
            result += line[i];
        end = start;
    }
    result += '\n';
    if (!write_all(STDOUT_FILENO, result)) return false;
    line.clear();
    return true;
}

int main()
{
    string line;
    while (true)
    {
        int state = read_line(line);
        if (state == 0) break;
        if (state == -1)
        {
            print_error("read failed");
            return 1;
        }
        if (!send_reversed(line))
        {
            print_error("write failed");
            return 1;
        }
    }
    return 0;
}
