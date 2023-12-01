import socket
import threading
import argparse
import time

MAX_CLIENTS = 50

# Mutexes and Conditions for producer-consumer pattern
list_lock = threading.Lock()
client_cv = threading.Condition(lock=list_lock)
analysis_cv = threading.Condition(lock=list_lock)
#Mutex for book frequency
frequency_lock = threading.Lock()

n = 100     #buffer size(from tutorial)
In = 0
Out = 0
updated = False

class Node:
    def __init__(self, data):
        self.data = data
        self.next = None
        self.book_next = None
        self.book_title = None
        self.searched = False

class Link_List:
    def __init__(self):
        self.head = None
Shared_Link_List = Link_List()

class BookFrequency:
    def __init__(self):
        self.title = None
        self.frequency = 0
bookFrequencyArray = [BookFrequency() for _ in range(MAX_CLIENTS)]

# Function to count the frequency of pattern in a string
def count_substring(main_str, substr):
    return main_str.count(substr)

def handle_client(client_socket, client_id):
    try:
        client_data = b''
        book_head = None  # Keep track of the book head for this client

        while True:
            chunk = client_socket.recv(1024)
            if not chunk:
                break
            client_data += chunk

            # Check if a full line has been received
            while b'\n' in client_data:
                line, client_data = client_data.split(b'\n', 1)
                new_node = Node(line.decode('utf-8'))
                with list_lock:
                    global In
                    while (In + 1) % n == Out:
                        print('Client goes to sleep')
                        client_cv.wait()

                    # Add the new node to the linked list
                    if Shared_Link_List.head is None:
                        Shared_Link_List.head = new_node
                    else:
                        current = Shared_Link_List.head
                        while current.next is not None:
                            current = current.next
                        current.next = new_node

                    if book_head is None:
                        book_head = new_node
                        new_node.book_title = new_node.data
                        bookFrequencyArray[client_id - 1].title = new_node.data
                        bookFrequencyArray[client_id - 1].frequency = 0
                    else:
                        current = book_head
                        while current.book_next is not None:
                            current = current.book_next
                        current.book_next = new_node
                        new_node.book_title = book_head.data

                    In = (In + 1) % n
                    analysis_cv.notify()

                    # Print a message whenever a node is added
                    print(f"Added node from client {client_id} to shared list: {line.decode('utf-8')}")

    except Exception as e:
        print(f"An error occurred with client {client_id}: {str(e)}")
    finally:
        # Write the received book head to a file when the connection is closed
        if book_head is not None:
            book_filename = f"book_{client_id:02}.txt"
            with open(book_filename, "w", encoding="utf-8") as book_file:
                current = book_head
                while current.book_next is not None:
                    book_file.write(current.data + '\n')
                    current = current.book_next
        print(f"Connection from client {client_id} closed. Book written to {book_filename}.")
        client_socket.close()

def analysis_thread(pattern):
    last_processed = None
    while True:
        with list_lock:
            global Out
            while In == Out:
                #print('Analysis thread go to sleep')
                analysis_cv.wait()

            if last_processed is None:
                current = Shared_Link_List.head
            else:
                current = last_processed

            while current is not None:
                if not current.searched:
                    count = count_substring(current.data, pattern)
                    if count > 0:
                        title = current.book_title
                        with frequency_lock:
                            for book in bookFrequencyArray:
                                if book.title == title:
                                    book.frequency += count
                                    global updated
                                    updated = True
                    current.searched = True

                Out = (Out + 1) % n
                client_cv.notify()
                last_processed = current

                if current.next is not None:
                    current = current.next
                else:
                    break

# Function to print the frequency data every 5 seconds
def print_frequency(pattern):
    while True:
        time.sleep(5)
        with frequency_lock:
            global updated
            if updated:
                sorted_books = sorted(bookFrequencyArray, key=lambda x: -x.frequency)
                for book in sorted_books:
                    if book.frequency > 0:
                        book_title = book.title.strip()
                        print(f"Book Title: {book_title}\nSearch Pattern: {pattern}\nFrequency: {book.frequency}")
                updated = False

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-l', '--port', type=int, required=True)
    parser.add_argument('-p', '--pattern', type=str, required=True)
    args = parser.parse_args()

    host = 'localhost'
    port = args.port
    pattern = args.pattern

    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind((host, port))
    server_socket.listen(5)

    print(f"Server is listening on {host}:{port}")

    for _ in range(2):
        thread = threading.Thread(target=analysis_thread, args=(pattern,))
        thread.start()

    print_frequency_thread = threading.Thread(target=print_frequency, args=(pattern,))
    print_frequency_thread.start()
    
    client_id = 1

    try:
        while True:
            client_socket, client_address = server_socket.accept()
            print(f"Accepted connection from {client_address}")
            client_thread = threading.Thread(target=handle_client, args=(client_socket, client_id))
            client_thread.start()
            client_id += 1
    except KeyboardInterrupt:
        print("Server is interrupted. Server Closed...")
    finally:
        server_socket.close()

if __name__ == "__main__":
    main()