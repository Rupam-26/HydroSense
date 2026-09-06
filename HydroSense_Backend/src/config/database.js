const mongoose =
    require(
        "mongoose"
    );

let listenersInstalled =
    false;

function installConnectionListeners()
{
    if (
        listenersInstalled
    )
    {
        return;
    }

    listenersInstalled =
        true;

    mongoose.connection.on(
        "connected",
        () =>
        {
            console.log(
                "[MongoDB] CONNECTED"
            );
        }
    );

    mongoose.connection.on(
        "disconnected",
        () =>
        {
            console.warn(
                "[MongoDB] DISCONNECTED"
            );
        }
    );

    mongoose.connection.on(
        "reconnected",
        () =>
        {
            console.log(
                "[MongoDB] RECONNECTED"
            );
        }
    );

    mongoose.connection.on(
        "error",
        (error) =>
        {
            console.error(
                "[MongoDB] Runtime error:",
                error.message
            );
        }
    );
}

async function connectDatabase()
{
    if (
        !process.env.MONGODB_URI
    )
    {
        throw new Error(
            "MONGODB_URI is missing from .env"
        );
    }

    installConnectionListeners();

    console.log("");
    console.log(
        "===================================================="
    );
    console.log(
        "          CONNECTING TO MONGODB ATLAS"
    );
    console.log(
        "===================================================="
    );

    const connection =
        await mongoose.connect(
            process.env.MONGODB_URI,
            {
                serverSelectionTimeoutMS:
                    15000,

                connectTimeoutMS:
                    15000,

                socketTimeoutMS:
                    30000,

                maxPoolSize:
                    10,

                minPoolSize:
                    0
            }
        );

    console.log(
        `[MongoDB] Host     : ${connection.connection.host}`
    );

    console.log(
        `[MongoDB] Database : ${connection.connection.name}`
    );

    console.log(
        `[MongoDB] State    : ${connection.connection.readyState}`
    );

    console.log(
        "===================================================="
    );

    console.log("");

    return connection;
}

module.exports =
    connectDatabase;