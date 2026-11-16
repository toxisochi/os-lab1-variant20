#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <time.h>

const int MAX_POINTS = 10000;
const int MAX_THREADS = 1024;

struct Point
{
    double x;
    double y;
    double z;
};

struct Result
{
    int a;
    int b;
    int c;
    double value;
};

struct Task
{
    Point* points;
    int n;
    int begin;
    int step;
    Result result;
};

bool write_text(int fd, const char* text)
{
    int length = 0;
    while(text[length] != '\0') length++;
    int offset = 0;
    while(offset < length)
    {
        ssize_t sent = write(fd, text + offset, length - offset);
        if(sent == -1 && errno == EINTR) continue;
        if(sent <= 0) return false;
        offset += sent;
    }
    return true;
}

void print_int(long long value)
{
    char text[32];
    int size = 0;
    do
    {
        text[size++] = '0' + value % 10;
        value /= 10;
    } while(value > 0);
    for(int i = 0; i < size / 2; i++)
    {
        char temp = text[i];
        text[i] = text[size - i - 1];
        text[size - i - 1] = temp;
    }
    text[size] = '\0';
    if(!write_text(STDOUT_FILENO, text)) _exit(1);
}

void print_double(double value)
{
    if(value < 0)
    {
        if(!write_text(STDOUT_FILENO, "-")) _exit(1);
        value = -value;
    }
    long long scaled = static_cast<long long>(value * 1000000.0 + 0.5);
    print_int(scaled / 1000000);
    char fraction[8] = ".000000";
    long long rest = scaled % 1000000;
    for(int i = 6; i >= 1; i--)
    {
        fraction[i] = '0' + rest % 10;
        rest /= 10;
    }
    if(!write_text(STDOUT_FILENO, fraction)) _exit(1);
}

bool space(char c)
{
    return c == ' ' || c == '\n' || c == '\r' ||
           c == '\t' || c == '\v' || c == '\f';
}

int read_token(char* token)
{
    static char buffer[4096];
    static int position = 0;
    static int size = 0;
    int length = 0;
    while(true)
    {
        if(position == size)
        {
            ssize_t received = read(STDIN_FILENO, buffer, sizeof(buffer));
            if(received == -1 && errno == EINTR) continue;
            if(received == -1) return -1;
            size = received;
            position = 0;
            if(size == 0)
            {
                token[length] = '\0';
                if(length == 0) return 0;
                return 1;
            }
        }
        char c = buffer[position++];
        if(space(c))
        {
            if(length > 0)
            {
                token[length] = '\0';
                return 1;
            }
        }
        else
        {
            if(length >= 127 || c == '\0') return -1;
            token[length++] = c;
        }
    }
}

int parse_int(const char* text, int maximum)
{
    if(text[0] == '\0') return -1;
    int value = 0;
    for(int i = 0; text[i] != '\0'; i++)
    {
        if(text[i] < '0' || text[i] > '9') return -1;
        int digit = text[i] - '0';
        if(value > (maximum - digit) / 10) return -1;
        value = value * 10 + digit;
        if(value > maximum) return -1;
    }
    return value;
}

bool read_coordinate(double& value)
{
    char token[128];
    if(read_token(token) != 1) return false;
    char* end = nullptr;
    errno = 0;
    value = strtod(token, &end);
    return errno != ERANGE && end != token && *end == '\0' &&
           isfinite(value) && fabs(value) <= 10000;
}

double area_squared4(Point a, Point b, Point c)
{
    double ux = b.x - a.x;
    double uy = b.y - a.y;
    double uz = b.z - a.z;
    double vx = c.x - a.x;
    double vy = c.y - a.y;
    double vz = c.z - a.z;
    double wx = uy * vz - uz * vy;
    double wy = uz * vx - ux * vz;
    double wz = ux * vy - uy * vx;
    return wx * wx + wy * wy + wz * wz;
}

bool better(Result candidate, Result best)
{
    if(candidate.value != best.value) return candidate.value > best.value;
    if(candidate.a != best.a) return candidate.a < best.a;
    if(candidate.b != best.b) return candidate.b < best.b;
    return candidate.c < best.c;
}

Result find_max_range(Point* points, int n, int begin, int step)
{
    Result best = {-1, -1, -1, -1.0};
    for(int i = begin; i < n - 2; i += step)
    {
        for(int j = i + 1; j < n; j++)
        {
            for(int k = j + 1; k < n; k++)
            {
                double value = area_squared4(points[i], points[j], points[k]);
                if(value > best.value)
                {
                    best.a = i;
                    best.b = j;
                    best.c = k;
                    best.value = value;
                }
            }
        }
    }
    return best;
}

Result find_max_sequential(Point* points, int n)
{
    return find_max_range(points, n, 0, 1);
}

void* worker(void* arg)
{
    Task* task = static_cast<Task*>(arg);
    task->result = find_max_range(task->points, task->n, task->begin, task->step);
    return nullptr;
}

bool find_max_parallel(Point* points, int n, int count, Result& best)
{
    Task tasks[MAX_THREADS];
    pthread_t threads[MAX_THREADS];
    for(int i = 0; i < count; i++)
    {
        tasks[i].points = points;
        tasks[i].n = n;
        tasks[i].begin = i;
        tasks[i].step = count;
        tasks[i].result = {-1, -1, -1, -1.0};
    }
    int created = 0;
    for(int i = 1; i < count; i++)
    {
        if(pthread_create(&threads[i - 1], nullptr, worker, &tasks[i]) != 0)
        {
            for(int j = 0; j < created; j++) pthread_join(threads[j], nullptr);
            write_text(STDERR_FILENO, "pthread_create failed\n");
            _exit(1);
        }
        created++;
    }
    worker(&tasks[0]);
    bool joined = true;
    for(int i = 0; i < created; i++)
    {
        if(pthread_join(threads[i], nullptr) != 0) joined = false;
    }
    if(!joined)
    {
        write_text(STDERR_FILENO, "pthread_join failed\n");
        _exit(1);
    }
    best = tasks[0].result;
    for(int i = 1; i < count; i++)
    {
        if(better(tasks[i].result, best)) best = tasks[i].result;
    }
    return true;
}

int main(int argc, char* argv[])
{
    bool sequential = false;
    int count = 0;
    if(argc == 2 && argv[1][0] == '-' && argv[1][1] == 's' && argv[1][2] == '\0')
    {
        sequential = true;
        count = 1;
    }
    else if(argc == 3 && argv[1][0] == '-' && argv[1][1] == 't' && argv[1][2] == '\0')
    {
        count = parse_int(argv[2], MAX_THREADS);
    }
    if(count < 1)
    {
        write_text(STDERR_FILENO, "Usage: ./2lab -s OR ./2lab -t N (1..1024) < points.txt\n");
        return 1;
    }
    Point points[MAX_POINTS];
    char token[128];
    if(read_token(token) != 1) return 1;
    int n = parse_int(token, MAX_POINTS);
    if(n < 3)
    {
        write_text(STDERR_FILENO, "Point count must be 3..10000\n");
        return 1;
    }
    for(int i = 0; i < n; i++)
    {
        if(!read_coordinate(points[i].x) || !read_coordinate(points[i].y) ||
           !read_coordinate(points[i].z))
        {
            write_text(STDERR_FILENO, "Invalid coordinates: finite numbers, abs <= 10000\n");
            return 1;
        }
    }
    if(read_token(token) != 0)
    {
        write_text(STDERR_FILENO, "Extra input or read error\n");
        return 1;
    }
    if(count > n - 2) count = n - 2;
    Result best;
    timespec start, finish;
    if(clock_gettime(CLOCK_MONOTONIC, &start) == -1) return 1;
    if(sequential) best = find_max_sequential(points, n);
    else if(!find_max_parallel(points, n, count, best)) return 1;
    if(clock_gettime(CLOCK_MONOTONIC, &finish) == -1) return 1;
    double milliseconds = (finish.tv_sec - start.tv_sec) * 1000.0 +
                          (finish.tv_nsec - start.tv_nsec) / 1000000.0;

    if(!write_text(STDOUT_FILENO, "Indices: ")) return 1;
    print_int(best.a);
    write_text(STDOUT_FILENO, " ");
    print_int(best.b);
    write_text(STDOUT_FILENO, " ");
    print_int(best.c);
    write_text(STDOUT_FILENO, "\nArea: ");
    print_double(sqrt(best.value) / 2.0);
    write_text(STDOUT_FILENO, "\nThreads: ");
    print_int(count);
    write_text(STDOUT_FILENO, "\nTime_ms: ");
    print_double(milliseconds);
    if(!write_text(STDOUT_FILENO, "\n")) return 1;
    return 0;
}
