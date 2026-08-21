from fastapi import (
    FastAPI,
    WebSocket,
    WebSocketDisconnect,
    Request,
    Depends
)
from fastapi.responses import HTMLResponse
from sqlalchemy.orm import Session
from models import Order, OrderItem, Payment

import uvicorn
import json
from uuid import uuid4
from datetime import datetime

from database import SessionLocal
from crud import get_products, create_order
from schemas import (
    ProductResponse,
    OrderCreateRequest,
)

app = FastAPI()


# ============================================================
# DATABASE
# ============================================================

def get_db():
    db = SessionLocal()

    try:
        yield db
    finally:
        db.close()


# ============================================================
# PRODUCTS
# ============================================================

@app.get("/products", response_model=list[ProductResponse])
def products(db: Session = Depends(get_db)):
    return get_products(db)


# ============================================================
# WEBSOCKET CONNECTION MANAGER
# ============================================================

class ConnectionManager:

    def __init__(self):
        self.active_connections: dict[str, WebSocket] = {}

    async def connect(
        self,
        machine_id: str,
        websocket: WebSocket
    ):
        await websocket.accept()

        self.active_connections[machine_id] = websocket

        print(
            f"Machine {machine_id} connected"
        )

    def disconnect(self, machine_id: str):

        if machine_id in self.active_connections:
            del self.active_connections[machine_id]

            print(
                f"Machine {machine_id} disconnected"
            )

    async def send_command(
        self,
        machine_id: str,
        message: dict
    ):

        if machine_id in self.active_connections:

            await self.active_connections[
                machine_id
            ].send_text(
                json.dumps(message)
            )

            return True

        return False


manager = ConnectionManager()


# ============================================================
# ROOT
# ============================================================

@app.get("/")
async def root_redirect():

    return {
        "status": "ok",
        "message": "Backend running."
    }


# ============================================================
# CREATE ORDER
# ============================================================

@app.post("/order")
async def create_order_endpoint(
    order_data: OrderCreateRequest,
    request: Request,
    db: Session = Depends(get_db)
):

    if not order_data.machine_id:

        return {
            "status": "error",
            "message": "Machine ID is required."
        }

    if not order_data.items:

        return {
            "status": "error",
            "message": "Cart is empty."
        }

    order_id = uuid4().hex[:10]

    try:

        order = create_order(
            db=db,
            order_id=order_id,
            machine_id=order_data.machine_id,
            items=order_data.items
        )

    except ValueError as error:

        return {
            "status": "error",
            "message": str(error)
        }

    checkout_url = (
        str(request.url_for("checkout_page"))
        + f"?order_id={order_id}"
    )

    return {
        "status": "success",
        "order_id": order.id,
        "total": order.total,
        "payment_url": checkout_url
    }


# ============================================================
# CHECKOUT PAGE
# ============================================================

@app.get(
    "/checkout",
    response_class=HTMLResponse
)
async def checkout_page(
    request: Request
):

    return """
    <!DOCTYPE html>

    <html>

    <head>

        <meta charset="UTF-8">

        <meta
            name="viewport"
            content="width=device-width, initial-scale=1.0"
        >

        <title>Vending Payment</title>

        <style>

            body {
                font-family: Arial, sans-serif;
                background: #111;
                color: #fff;
                margin: 0;
            }

            .card {
                max-width: 440px;
                margin: 40px auto;
                background: #1e293b;
                border-radius: 16px;
                padding: 24px;
            }

            h1 {
                margin: 0 0 14px;
            }

            .amount {
                font-size: 32px;
                font-weight: bold;
                margin: 20px 0;
            }

            label {
                display: block;
                margin-top: 16px;
                color: #94a3b8;
            }

            input {
                width: 100%;
                box-sizing: border-box;
                padding: 12px;
                margin-top: 8px;
                border-radius: 10px;
                border: 1px solid #334155;
                background: #0f172a;
                color: #fff;
            }

            button {
                width: 100%;
                padding: 14px;
                margin-top: 20px;
                border: none;
                border-radius: 10px;
                background: #2563eb;
                color: #fff;
                font-size: 16px;
                cursor: pointer;
            }

            .status {
                margin-top: 16px;
                font-weight: 600;
            }

        </style>

    </head>


    <body>

        <div class="card">

            <h1>Complete Payment</h1>

            <p id="summary">
    Loading order...
</p>

<div id="items"></div>

<div
    class="amount"
    id="amount"
>
    --
</div>

            <label for="phone">
                Phone number
            </label>

            <input
                id="phone"
                type="text"
                placeholder="0712345678"
            >

            <button id="payBtn">
                Pay Now
            </button>

            <div
                id="status"
                class="status"
            ></div>

        </div>


        <script>

            const query =
                new URLSearchParams(
                    window.location.search
                );

            const orderId =
                query.get("order_id");


            const summaryEl =
                document.getElementById(
                    "summary"
                );

            const amountEl =
                document.getElementById(
                    "amount"
                );

                const itemsEl =
    document.getElementById(
        "items"
    );

            const statusEl =
                document.getElementById(
                    "status"
                );


            // Load the order from the backend

            async function loadOrder() {

                if (!orderId) {

                    summaryEl.textContent =
                        "No order ID found.";

                    return;
                }


                try {

                    const response =
                        await fetch(
                            "/order/" + orderId
                        );


                    const data =
                        await response.json();


                    if (
                        data.status !==
                        "success"
                    ) {

                        summaryEl.textContent =
                            data.message ||
                            "Order not found.";

                        return;
                    }


                    const order =
                        data.order;


                    summaryEl.textContent =
    "Order ID: " + order.id;


itemsEl.innerHTML = "";


order.items.forEach(item => {

    const row =
        document.createElement("div");

    row.style.display = "flex";
    row.style.justifyContent =
        "space-between";

    row.style.marginBottom = "10px";

    row.innerHTML = `
        <span>
            Product ${item.product_id}
            × ${item.quantity}
        </span>

        <span>
            ${(
                item.unit_price *
                item.quantity
            ).toLocaleString()} TZS
        </span>
    `;

    itemsEl.appendChild(row);

});


amountEl.textContent =
    Number(
        order.total
    ).toLocaleString()
    + " TZS";


                } catch (error) {

                    summaryEl.textContent =
                        "Unable to load order.";

                }

            }


            loadOrder();


            // Payment button

            document
                .getElementById("payBtn")
                .addEventListener(
                    "click",
                    async () => {

                        const phone =
                            document
                                .getElementById(
                                    "phone"
                                )
                                .value
                                .trim();


                        if (
                            !orderId ||
                            !phone
                        ) {

                            statusEl.textContent =
                                "Enter your phone number.";

                            return;
                        }


                        statusEl.textContent =
                            "Processing payment...";


                        try {

                            const response =
                                await fetch(
                                    "/pay",
                                    {
                                        method: "POST",

                                        headers: {
                                            "Content-Type":
                                                "application/json"
                                        },

                                        body:
                                            JSON.stringify({
                                                order_id:
                                                    orderId,

                                                phone:
                                                    phone
                                            })
                                    }
                                );


                            const data =
                                await response.json();


                            if (
                                data.status ===
                                "success"
                            ) {

                                statusEl.textContent =
                                    "Payment successful. Dispensing your item now.";

                            } else {

                                statusEl.textContent =
                                    data.message ||
                                    "Payment failed.";

                            }

                        } catch (error) {

                            statusEl.textContent =
                                "Payment request failed.";

                        }

                    }
                );

        </script>

    </body>

    </html>
    """


# ============================================================
# GET ORDER
# ============================================================

@app.get("/order/{order_id}")
async def get_order(
    order_id: str,
    db: Session = Depends(get_db)
):

    from models import Order, OrderItem

    order = (
        db.query(Order)
        .filter(Order.id == order_id)
        .first()
    )

    if not order:

        return {
            "status": "error",
            "message": "Order not found."
        }


    items = (
        db.query(OrderItem)
        .filter(
            OrderItem.order_id ==
            order_id
        )
        .all()
    )


    return {

        "status": "success",

        "order": {

            "id": order.id,

            "machine_id":
                order.machine_id,

            "status":
                order.status,

            "total":
                order.total,

            "items": [

                {
                    "product_id":
                        item.product_id,

                    "quantity":
                        item.quantity,

                    "unit_price":
                        item.unit_price
                }

                for item in items

            ]

        }

    }


# ============================================================
# PAYMENT
# ============================================================

@app.post("/pay")
async def pay_endpoint(
    req: Request,
    db: Session = Depends(get_db)
):

    from models import Order, Payment

    data = await req.json()

    order_id = data.get("order_id")
    phone = data.get("phone")


    if not order_id or not phone:

        return {
            "status": "error",
            "message":
                "Order ID and phone are required."
        }


    order = (
        db.query(Order)
        .filter(
            Order.id == order_id
        )
        .first()
    )


    if not order:

        return {
            "status": "error",
            "message":
                "Order not found."
        }


    if order.status != "pending":

        return {
            "status": "error",
            "message":
                "Order is not pending payment."
        }


    # --------------------------------------------------------
    # TEMPORARY PAYMENT SIMULATION
    # --------------------------------------------------------

    payment = Payment(
        order_id=order.id,
        phone=phone,
        status="paid"
    )

    db.add(payment)


    order.status = "paid"


    db.commit()


    # --------------------------------------------------------
    # SEND DISPENSE COMMAND TO ESP32
    # --------------------------------------------------------

    payload = {

        "action":
            "dispense",

        "order_id":
            order.id,

        "items": []

    }


    from models import OrderItem

    items = (
        db.query(OrderItem)
        .filter(
            OrderItem.order_id ==
            order.id
        )
        .all()
    )


    for item in items:

        payload["items"].append({

            "product_id":
                item.product_id,

            "quantity":
                item.quantity

        })


    success = await manager.send_command(
        order.machine_id,
        payload
    )


    if success:

        return {

            "status":
                "success",

            "message":
                "Payment successful and dispense signal sent.",

            "order_id":
                order.id,

            "total":
                order.total

        }


    return {

        "status":
            "success",

        "message":
            "Payment recorded, but vending machine is not connected.",

        "order_id":
            order.id,

        "total":
            order.total

    }


# ============================================================
# PAYMENT GET
# ============================================================

@app.get("/pay")
async def pay_get():

    return {

        "status": "ok",

        "message":
            "Use POST /pay after order creation."

    }


# ============================================================
# ESP32 WEBSOCKET
# ============================================================

@app.websocket(
    "/ws/{machine_id}"
)
async def websocket_endpoint(
    websocket: WebSocket,
    machine_id: str
):

    await manager.connect(
        machine_id,
        websocket
    )

    try:

        while True:

            await websocket.receive_text()

    except WebSocketDisconnect:

        manager.disconnect(
            machine_id
        )


# ============================================================
# START SERVER
# ============================================================

if __name__ == "__main__":

    uvicorn.run(
        app,
        host="0.0.0.0",
        port=8000
    )