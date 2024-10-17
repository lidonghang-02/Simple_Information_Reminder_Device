#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *getcgidata(FILE *fp, char *requestmethod);

char ToChar(char a, char b)
{
    char c = 0;

    if (a >= '0' && a <= '9')
    {
        c = a - '0';
    }
    else if (a >= 'a' && a <= 'z')
    {
        c = a - 'a' + 10;
    }
    else if (a >= 'A' && a <= 'Z')
    {
        c = a - 'A' + 10;
    }

    if (b >= '0' && b <= '9')
    {
        c = c * 16 + b - '0';
    }
    else if (b >= 'a' && b <= 'z')
    {
        c = c * 16 + b - 'a' + 10;
    }
    else if (b >= 'A' && b <= 'Z')
    {
        c = c * 16 + b - 'A' + 10;
    }

    return c;
}

int AToI(const char *src, char *des)
{
    if (NULL == src || NULL == des)
    {
        return -1;
    }

    int i, j;
    int strLen = strlen(src);

    for (i = 0, j = 0; i < strLen;)
    {
        if (src[i] == '%')
        {
            des[j++] = ToChar(src[i + 1], src[i + 2]);
            i += 3;
        }
        else
        {
            des[j++] = src[i++];
        }
    }

    return 0;
}

void logData(const char *data)
{
    FILE *logFile = fopen("web.log", "w"); // 以追加模式打开日志文件
    if (logFile != NULL)
    {
        fprintf(logFile, "%s\n", data); // 将数据写入日志文件
        fclose(logFile);                // 关闭文件
    }
    else
    {
        perror("Unable to open log file");
    }
}

int main()
{
    char *input;
    char *req_method;
    int hour0, min0, hour1, min1;
    char data0[128] = {0};
    char utf8_data0[128] = {0};

    time_t *timep;
    printf("Content-type: text/html;charset=utf-8\n\n");
    timep = malloc(sizeof(*timep));
    time(timep);
    char *s = ctime(timep);
    printf("<br>当前时间:%s</br>", s);
    printf("<br>配置成功!</br>");
    req_method = getenv("REQUEST_METHOD");
    input = getcgidata(stdin, req_method);
    sscanf(input, "hour0=%d&min0=%d&hour1=%d&min1=%d&data0=%[^&]", &hour0, &min0, &hour1, &min1, data0);

    AToI(data0, utf8_data0);
    printf("<br>从%d:%d到%d点%d提醒内容为:%s</br>", hour0, min0, hour1, min1, utf8_data0);

    char logEntry[512];
    snprintf(logEntry, sizeof(logEntry), "%d:%d:%d:%d:%s", hour0, min0, hour1, min1, data0);
    logData(logEntry); // 记录日志

    free(timep);
    free(input);
    return 0;
}
char *getcgidata(FILE *fp, char *requestmethod)
{
    char *input;
    int len;
    int size = 1024;
    int i = 0;

    if (!strcmp(requestmethod, "GET"))
    {
        input = getenv("QUERY_STRING");
        return input;
    }
    else if (!strcmp(requestmethod, "POST"))
    {
        len = atoi(getenv("CONTENT_LENGTH"));
        input = (char *)malloc(sizeof(char) * (size + 1));

        if (len == 0)
        {
            input[0] = '\0';
            return input;
        }

        while (1)
        {
            input[i] = (char)fgetc(fp);
            if (i == size)
            {
                input[i + 1] = '\0';
                return input;
            }

            --len;
            if (feof(fp) || (!(len)))
            {
                i++;
                input[i] = '\0';
                return input;
            }
            i++;
        }
    }
    return NULL;
}