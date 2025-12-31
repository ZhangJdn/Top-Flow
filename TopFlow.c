/*
Top Flow Quantitative Screening Bot is a C based bot that ranks
equities by live trading activity measured by a custom metric - "flow score"
(price-change percentage * trade volume) and sends an alert to a discord channel 
every 30 minutes

This version is used for API calls not local calls.

NOTE: This by no means is an indicator to buy/sell any financial asset. Solely used for education purposes. This is not financial advice.

https://github.com/ZhangJdn/Top-Flow
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <math.h>

#define CMD_SIZE 512
#define WAIT_THIRTY_MINUTES_BETWEEN_ALERTS 1800000

/*
Get numeric metrics from JSON by searching for a "key"
Ignores whitespace until it reaches the key (Values)
Example:
Get Volume:
finds ... "volume": 1234567 ... -> returns 1234567
*/
double get_data(const char *text, const char *key)
{
    char *current_index = strstr(text, key);

    if (!current_index)
    {
        return 0;
    }

    // Move the pointer forward so that it reaches the values
    current_index += strlen(key);

    // Ignore whitespace
    while (*current_index == ' ' || *current_index == ':' || *current_index == '"')
        current_index++;

    return atof(current_index);
}

/*
Fetch data from URL by running curl.
*/
char *get(const char *url)
{
    char cmd[500];
    sprintf(cmd, "curl -s \"%s\"", url);

    FILE *curl_output = popen(cmd, "r");

    if (!curl_output)
    {
        return NULL;
    }

    char *fetch = malloc(8192);

    if (!fetch)
    {
        pclose(curl_output);
        return NULL;
    }

    // Temp buffer
    char buffer[256];
    fetch[0] = '\0';

    while (fgets(buffer, sizeof(buffer), curl_output))
    {
        strcat(fetch, buffer);
    }

    pclose(curl_output);
    return fetch;
}

// Send Discord Alert by discord webhook using curl and JSON
void send_discord_alert(const char *webhook, const char *message)
{
    char safe_message[1000]; // Message buffer
    int index = 0;

    for (int i = 0; message[i] != '\0'; i++)
    {
        // Prevent buffer overlow
        if (index >= (int)sizeof(safe_message) - 5) // 5 is a safety buffer to gurantee we always have room
            break;

        // JSON does not allow newline characters so we must must convert into one line
        if (message[i] == '\n')
        {
            safe_message[index++] = '\\';
            safe_message[index++] = 'n';
        }
        else
        {
            safe_message[index++] = message[i];
        }
    }

    safe_message[index] = '\0';
    char buff[1500]; // Buffer

    sprintf(buff,
            "curl -s -X POST \"%s\" -H \"Content-Type: application/json\" -d \"{\\\"content\\\":\\\"%s\\\"}\"",
            webhook, safe_message);

    system(buff);
}

/*
Run each ticker once, calculating each metric, prints the results, and sends a Discord alert
*/
void run_once(const char *api_key, const char *webhook)
{
    const char *tickers[8] =
        {
            "AAPL", "MSFT", "NVDA", "META",
            "AMZN", "AMD", "GOOGL", "TSLA"};

    double      top_flow       = 0.0;
    double      top_price      = 0.0;
    double      top_change_pct = 0.0;
    double      top_rvol       = 0.0;
    double      top_volume     = 0.0;
    const char *top_ticker     = NULL; // Has not been determined yet, so NULL for now

    printf("Fetching tickers...\n");

    for (int i = 0; i < 8; i++)
    {
        const char *symbol = tickers[i];

        char url[CMD_SIZE];
        snprintf(url, CMD_SIZE,
                 "https://api.twelvedata.com/quote?symbol=%s&exchange=NASDAQ&apikey=%s",
                 symbol, api_key);

        char *json = get(url);

        if (!json)
        {
            continue;
        }

        // JSON status error
        if (strstr(json, "\"status\":\"error\""))
        {
            free(json);
            continue;
        }

        // Get numeric metrics
        double prev_close = get_data(json, "\"previous_close\"");
        double change     = get_data(json, "\"change\"");
        double volume     = get_data(json, "\"volume\"");
        double pct_change = get_data(json, "\"percent_change\"");
        double avg_volume = get_data(json, "\"average_volume\"");

        if (avg_volume <= 0)
        {
            free(json);
            continue;
        }

        // Previous close + change today = Current price; Twelve data API does not have a current price metric
        double price = prev_close + change;
        double relative_volume = volume / avg_volume;

        // Calculating flow score (% change * relative volume)
        double flow = pct_change * relative_volume;

        printf(
            "%s | Price: %.2f | Volume: %.0f | RVol %.4f | Change: %.4f%% | DirectionalFlow: %.4f\n",
            symbol, price, volume, relative_volume, pct_change, flow);

        // Selects the ticker with the highest absolute flow score. Tracks both BULLISH and BEARISH momentum
        if (!top_ticker || fabs(flow) > fabs(top_flow))
        {
            top_flow       = flow;
            top_price      = price;
            top_change_pct = pct_change;
            top_volume     = volume;
            top_rvol       = relative_volume;
            top_ticker     = symbol;
        }

        free(json);
    }

    if (!top_ticker)
    {
        return;
    }

    // Find out if its either TOP BULL or TOP BEAR
    const char *direction = (top_flow >= 0) ? "TOP BULL FLOW" : "TOP BEAR FLOW";

    printf("===== %s =====\n"           , direction);
    printf("Ticker: %s\n"               , top_ticker);
    printf("Price: %.2f\n"              , top_price);
    printf("Change: %.4f%%\n"           , top_change_pct);
    printf("Volume: %.0f\n"             , top_volume);            
    printf("Relative Volume: %.4f\n"    , top_rvol);
    printf("Directional Flow: %.4f\n\n" , top_flow);

    if (webhook)
    {
        char msg[500]; // message buffer
        snprintf(
            msg,
            sizeof(msg),
            "%s\n"
            "Ticker: %s\n"
            "Price: %.2f\n"
            "Change: %.4f%%\n"
            "Volume: %.0f\n"
            "RVol: %.4f\n"
            "Directional Flow: %.4f",
            direction,
            top_ticker,
            top_price,
            top_change_pct,
            top_volume,
            top_rvol,
            top_flow);

        send_discord_alert(webhook, msg);
    }
}

/*
Runs the program and verifies API key and wekhooks are set
Waits 30 minutes before sending another discord alert
*/
int main(void)
{
    const char *api_key = getenv("TWELVE_DATA_API_KEY");
    const char *webhook = getenv("DISCORD_WEBHOOK_URL");

    if (!api_key)
    {
        printf("Twelve Data API key not set\n");
        return 1;
    }

    if (!webhook)
    {
        printf("Discord Webhook URL not set (optional)");
        return 1;
    }

    printf("Top Flow Bot\n");

    while (1)
    {
        run_once(api_key, webhook);
        // Wait 30 minutes before sending the next discord alert
        Sleep(WAIT_THIRTY_MINUTES_BETWEEN_ALERTS);
    }

    return 0;
}
