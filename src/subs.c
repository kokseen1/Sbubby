#include <stdlib.h>

#include <subs.h>

static Sub *sub_head = NULL;

static void insert_ordered(Sub *sub_new)
{
    if (sub_head == NULL)
    {
        // First node
        sub_head = sub_new;
        sub_head->next = NULL;
        return;
    }

    // Timestamp is earlier than head
    if (sub_new->start_ts < sub_head->start_ts)
    {
        // Insert at beginning (new head)
        sub_new->next = sub_head;
        sub_head = sub_new;
    }
    else
    {
        // Ordered insert
        Sub *sub_curr = sub_head;
        while (sub_curr)
        {
            if (!sub_curr->next || sub_curr->next->start_ts > sub_new->start_ts)
            {
                sub_new->next = sub_curr->next;
                sub_curr->next = sub_new;
                break;
            }
            sub_curr = sub_curr->next;
        }
    }
}

void new_sub(const double ts)
{
    Sub *sub = (Sub *)malloc(sizeof(Sub));
    sub->start_ts = ts;
    sub->end_ts = ts + 30;

    insert_ordered(sub);
}
