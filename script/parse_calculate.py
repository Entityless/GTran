import sys

sum = 1
count = 0

if ( len(sys.argv) != 3 ) :
    print("2 arguments needed")
    sys.exit(2)

for num in range(int(sys.argv[1]), int(sys.argv[2])) :
    fname = "/data/aaron/test_gquery/gquery/output/outputworker" + str(num)
    file = open(fname, "r")

    for line in file :
        tokens = line.rstrip('\n').split(" ")
        for token in tokens :
            if ( token == "[Timer]" ):
                count += 1
                if ( tokens[2] == "ms"):
                    sum = sum * int(tokens[1])
                else:
                    sum = sum * float(int(tokens[1]) / 1000)
            elif (token == "[Error]" or token == "error" or token == "Error" or token == "[error]" or token == "Error:"):
                print(line)

print("Average Time : ", round(sum **(1.0 / count), 4), " ms");
