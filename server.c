#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int IP[4];

void printIP(FILE* out, IP ip)
{
    fprintf(out, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

int compareIPs(IP ip1, IP ip2)
{
    for (int i = 0; i < 4; i++) {
        if (ip1[i] != ip2[i])
            return ip1[i] - ip2[i];
    }

    return 0;
}

int parseIP(char* inp, IP ip)
{
    int length;
    if (sscanf(inp, "%d.%d.%d.%d%n", &ip[0], &ip[1], &ip[2], &ip[3], &length) != 4)
        return 0;
    if (length != strlen(inp))
        return 0;
    for (int i = 0; i < 4; i++) {
        if (ip[i] < 0 || ip[i] > 255)
            return 0;
    }

    return 1;
}

int parseIPRange(char* arguments, IP from, IP to)
{
    char* dash = strchr(arguments, '-');
    if (dash) {
        *dash = '\0';
        if (!parseIP(arguments, from))
            return 0;
        if (!parseIP(dash + 1, to))
            return 0;
        if (compareIPs(from, to) > 0)
            return 0;
    } else {
        if (!parseIP(arguments, from) || !parseIP(arguments, to))
            return 0;
    }

    return 1;
}

int parsePort(char* arguments, int* port)
{
    int length;
    if (sscanf(arguments, "%d%n", port, &length) != 1)
        return 0;
    if (*port < 0 || *port > 65535)
        return 0;
    if (length != strlen(arguments))
        return 0;

    return 1;
}

int parsePortRange(char* arguments, int* from, int* to)
{
    char* dash = strchr(arguments, '-');
    if (dash) {
        *dash = '\0';
        if (!parsePort(arguments, from) || !parsePort(dash + 1, to))
            return 0;
        if (*from > *to)
            return 0;
    } else {
        if (!parsePort(arguments, from))
            return 0;
        *to = *from;
    }

    return 1;
}

typedef struct {
    IP ip;
    int port;
} Query;

typedef struct {
    IP fromIp;
    IP toIp;
    int fromPort;
    int toPort;
    Query* queries;
    int queryCount;
    int queryCapacity;
} Rule;

void initializeRule(Rule* rule)
{
    rule->queryCount = 0;
    rule->queryCapacity = 10;
    rule->queries = malloc(rule->queryCapacity * sizeof(Query));
    if (rule->queries == NULL) {
        fprintf(stderr, "Unable to allocate memory for Rule->queries\n");
        exit(1);
    }
}

int parseRule(char* arguments, Rule* rule)
{
    char* space = strchr(arguments, ' ');
    if (space)
        *space = '\0';
    return space && parseIPRange(arguments, rule->fromIp, rule->toIp) && parsePortRange(space + 1, &rule->fromPort, &rule->toPort);
}

int isIpPortWithinRuleBounds(Rule* rule, IP ip, int port)
{
    return compareIPs(ip, rule->fromIp) >= 0 && compareIPs(ip, rule->toIp) <= 0 && port >= rule->fromPort && port <= rule->toPort;
}

void addQueryToRule(Rule* rule, Query query)
{
    if (rule->queryCount >= rule->queryCapacity) {
        int newCapacity = rule->queryCapacity * 2;

        Query* newQueries = realloc(rule->queries, newCapacity * sizeof(Query));
        if (newQueries == NULL) {
            fprintf(stderr, "Unable to allocate memory for Rule->queries\n");
            exit(1);
        }

        rule->queries = newQueries;
        rule->queryCapacity = newCapacity;
    }

    rule->queries[rule->queryCount] = query;
    rule->queryCount += 1;
}

typedef struct {
    pthread_rwlock_t rulesLock;
    Rule* rules;
    int ruleCount;
    int ruleCapacity;
    pthread_rwlock_t requestsLock;
    char** requests;
    int requestsCount;
    int requestsCapacity;
} Server;

void initializeServer(Server* server, int initialRuleCapacity, int initialRequestsCapacity)
{
    server->rules = malloc(initialRuleCapacity * sizeof(Rule));
    if (server->rules == NULL) {
        fprintf(stderr, "Unable to allocate memory for Server->rules\n");
        exit(1);
    }

    server->requests = malloc(initialRequestsCapacity * sizeof(char*));
    if (server->requests == NULL) {
        fprintf(stderr, "Unable to allocate memory for Server->queries");
        exit(1);
    }

    server->ruleCount = 0;
    server->ruleCapacity = initialRuleCapacity;
    server->requestsCount = 0;
    server->requestsCapacity = initialRequestsCapacity;

    if (pthread_rwlock_init(&server->rulesLock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize rulesLock");
        exit(1);
    }

    if (pthread_rwlock_init(&server->requestsLock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize requestsLock");
        exit(1);
    }
}

void addRuleToServer(Server* server, Rule rule)
{
    if (server->ruleCount >= server->ruleCapacity) {
        int newCapacity = server->ruleCapacity * 2;

        Rule* newRules = (Rule*)realloc(server->rules, newCapacity * sizeof(Rule));
        if (newRules == NULL) {
            fprintf(stderr, "Unable to allocate memory for Server->rules\n");
            exit(1);
        }

        server->rules = newRules;
        server->ruleCapacity = newCapacity;
    }

    server->rules[server->ruleCount] = rule;
    server->ruleCount += 1;
}

void removeRuleFromServer(Server* server, int ruleIndex)
{
    free(server->rules[ruleIndex].queries);

    // Shimmy shimmy yah, shimmy yam, shimmy yay
    for (int i = ruleIndex; i < server->ruleCount - 1; i++)
        server->rules[i] = server->rules[i + 1];

    server->ruleCount--;
}

void addRequestToServer(Server* server, char* request)
{
    char* requestCopy = malloc((strlen(request) + 1) * sizeof(char));
    strcpy(requestCopy, request);

    if (server->requestsCount >= server->requestsCapacity) {
        int newCapacity = server->requestsCapacity * 2;

        char** newRequests = realloc(server->requests, newCapacity * sizeof(char*));
        if (newRequests == NULL) {
            printf("Unable to allocate memory for Server->requests\n");
            exit(1);
        }

        server->requests = newRequests;
        server->requestsCapacity = newCapacity;
    }

    server->requests[server->requestsCount] = requestCopy;
    server->requestsCount += 1;
}

void processListAllRequests(Server* server, FILE* out)
{
    pthread_rwlock_rdlock(&server->requestsLock);
    for (int i = 0; i < server->requestsCount; i++)
        fprintf(out, "%s\n", server->requests[i]);
    pthread_rwlock_unlock(&server->requestsLock);
}

void processAddRule(Server* server, FILE* out, char* arguments)
{
    Rule rule;
    initializeRule(&rule);
    if (!parseRule(arguments, &rule)) {
        fprintf(out, "Invalid rule\n");
        free(rule.queries);
        return;
    }

    pthread_rwlock_wrlock(&server->rulesLock);
    addRuleToServer(server, rule);
    pthread_rwlock_unlock(&server->rulesLock);
    fprintf(out, "Rule added\n");
}

void processCheckAllowed(Server* server, FILE* out, char* arguments)
{
    Query query;

    char* space = strchr(arguments, ' ');
    if (space)
        *space = '\0';
    if (!space || !parseIP(arguments, query.ip) || !parsePort(space + 1, &query.port)) {
        fprintf(out, "Illegal IP address or port specified\n");
        return;
    }

    pthread_rwlock_rdlock(&server->rulesLock);
    for (int i = 0; i < server->ruleCount; i++) {
        Rule* rule = &server->rules[i];
        if (isIpPortWithinRuleBounds(rule, query.ip, query.port)) {
            pthread_rwlock_unlock(&server->rulesLock);
            pthread_rwlock_wrlock(&server->rulesLock);
            addQueryToRule(rule, query);
            pthread_rwlock_unlock(&server->rulesLock);
            fprintf(out, "Connection accepted\n");
            return;
        }
    }

    pthread_rwlock_unlock(&server->rulesLock);
    fprintf(out, "Connection rejected\n");
}

void processRemoveRule(Server* server, FILE* out, char* arguments)
{
    Rule mock;
    parseRule(arguments, &mock);

    pthread_rwlock_wrlock(&server->rulesLock);
    int removed = 0;
    for (int i = 0; i < server->ruleCount; i++) {
        Rule* rule = &server->rules[i];
        if (compareIPs(rule->fromIp, mock.fromIp) == 0 && compareIPs(rule->toIp, mock.toIp) == 0 && rule->fromPort == mock.fromPort && rule->toPort == mock.toPort) {
            removeRuleFromServer(server, i);
            i--;
            removed = 1;
        }
    }

    pthread_rwlock_unlock(&server->rulesLock);
    fprintf(out, removed ? "Rule deleted\n" : "Rule not found\n");
}

void processListAllRules(Server* server, FILE* out)
{
    pthread_rwlock_rdlock(&server->rulesLock);
    for (int i = 0; i < server->ruleCount; i++) {
        Rule rule = server->rules[i];

        fprintf(out, "Rule: ");
        printIP(out, rule.fromIp);
        if (compareIPs(rule.fromIp, rule.toIp)) {
            fprintf(out, "-");
            printIP(out, rule.toIp);
        }
        fprintf(out, " %d", rule.fromPort);
        if (rule.fromPort != rule.toPort)
            fprintf(out, "-%d", rule.toPort);
        fprintf(out, "\n");

        for (int j = 0; j < rule.queryCount; j++) {
            Query* query = &rule.queries[j];
            fprintf(out, "Query: ");
            printIP(out, query->ip);
            fprintf(out, " %d\n", query->port);
        }
    }

    pthread_rwlock_unlock(&server->rulesLock);
}

void processCommand(Server* server, FILE* out, char command, char* arguments)
{
    switch (command) {
    case 'R':
        processListAllRequests(server, out);
        break;
    case 'A':
        processAddRule(server, out, arguments);
        break;
    case 'C':
        processCheckAllowed(server, out, arguments);
        break;
    case 'D':
        processRemoveRule(server, out, arguments);
        break;
    case 'L':
        processListAllRules(server, out);
        break;
    default:
        fprintf(out, "Illegal request\n");
        break;
    }
}

void processInput(Server* server, FILE* out, char* input)
{
    char command;
    char* arguments = NULL;
    sscanf(input, "%c %m[^\n]", &command, &arguments);

    processCommand(server, out, command, arguments == NULL ? "" : arguments);
    free(arguments);
}

void launchInteractiveMode()
{
    Server server;
    initializeServer(&server, 10, 10);

    char* input = NULL;
    size_t input_length;
    while (getline(&input, &input_length, stdin) != -1) {
        input[strcspn(input, "\n")] = '\0';
        addRequestToServer(&server, input);
        processInput(&server, stdout, input);
    }

    free(input);
}

typedef struct {
    int newsockfd;
    Server* server;
} ClientArgs;

void* handleClient(void* arg)
{
    ClientArgs* clientArgs = (ClientArgs*)arg;
    int newsockfd = clientArgs->newsockfd;
    Server* server = clientArgs->server;
    free(clientArgs);

    FILE* stream = fdopen(newsockfd, "r+");
    if (!stream) {
        fprintf(stderr, "Error opening client stream");
        close(newsockfd);
        exit(1);
    }

    char* buffer = NULL;
    size_t len = 0;

    if (getline(&buffer, &len, stream) == -1) {
        free(buffer);
        fclose(stream);
        return NULL;
    }

    buffer[strcspn(buffer, "\n")] = '\0';

    addRequestToServer(server, buffer);
    processInput(server, stream, buffer);

    free(buffer);
    fflush(stream);
    fclose(stream);

    return NULL;
}

void launchServer(int port)
{
    Server server;
    initializeServer(&server, 10, 10);

    struct sockaddr_in6 serv_addr, cli_addr;
    socklen_t clilen;
    int sockfd;

    sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error opening socket");
        exit(1);
    }

    memset((char*)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin6_family = AF_INET6;
    serv_addr.sin6_addr = in6addr_any;
    serv_addr.sin6_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Error on binding");
        close(sockfd);
        exit(1);
    }

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    while (1) {
        int newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);
        if (newsockfd < 0) {
            fprintf(stderr, "Error on accept");
            close(sockfd);
            exit(1);
        }

        ClientArgs* clientArgs = malloc(sizeof(ClientArgs));
        clientArgs->newsockfd = newsockfd;
        clientArgs->server = &server;

        pthread_t clientThread;
        if (pthread_create(&clientThread, NULL, handleClient, clientArgs) != 0) {
            fprintf(stderr, "Error creating thread");
            free(clientArgs);
            close(newsockfd);
            continue;
        }
        pthread_detach(clientThread);
    }

    close(sockfd);
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s -i / <port>\n", argv[0]);
        return 1;
    }

    char* first = argv[1];
    if (!strcmp(first, "-i")) {
        launchInteractiveMode();
    } else {
        int port = atoi(first);
        launchServer(port);
    }

    return 0;
}
