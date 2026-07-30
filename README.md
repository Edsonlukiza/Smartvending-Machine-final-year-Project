Smart Vending Machine System 

Overview

The Smart Vending Machine System is an intelligent automated vending solution designed to improve the traditional vending experience by providing users with a fast, secure, and efficient way to purchase products.

The system allows customers to browse available products, select items, make payments, and receive their purchases automatically. It also provides an administrative interface for managing products, stock levels, sales records, and machine operations.

This project focuses on combining automation, user interaction, and smart management features to create a modern vending machine solution.

Features 
Customer Features
- Browse available products
- Search and filter products
- Add products to cart
- Make payments
- Receive purchased items
- View transaction history
- Receive notifications for successful purchases

Admin Features

- Admin dashboard
- Manage products
- Add new products
- Update product information
- Remove products
- View sales reports
- Monitor stock levels
- Manage vending machine settings

System Objectives 

The main objectives of this project are:

- Reduce manual vending operations
- Provide a simple and user-friendly purchasing process
- Improve inventory management
- Track sales and product performance
- Provide real-time vending machine monitoring


Technologies Used 

Frontend
- C++
- HTML5
- CSS3
- JavaScript

Backend
- PYTHON
- REST API

Database
- Firebase

Tools
- Git & GitHub
- VS Code
- ESP32 Module
- TFT Touch Screen


Project Structure


Smart-Vending-Machine/
│
├── frontend/
│   ├── index.html
│   ├── css/
│   └── js/
│
├── backend/
│   ├── controllers/
│   ├── models/
│   └── routes/
│
├── database/
│   └── vending_machine.sql
│
├── assets/
│
└── README.md




How It Works

1. User opens the vending machine interface
2. Available products are displayed
3. User selects a product
4. System checks product availability
5. User completes payment by scanning the QR-CODE
6. The machine releases the selected item
7. Transaction details are stored in the database



Database Design

Main entities include:

- Users
- Products
- Categories
- Inventory
- Transactions
- Payments
- Machine Status

The database is designed to store product information, customer transactions, and vending operations efficiently.


Installation Guide

Clone the repository

"git clone https://github.com/Edsonlukiza/Smart-Vending-Machine.git"

Navigate into the project

"cd Smart-Vending-Machine"


Setup Database

1. Open MySQL
2. Create a database:

```sql
CREATE DATABASE smart_vending_machine;
```

3. Import the SQL file located in the database folder.

Run the Project

For PHP projects:

```
Start Apache and MySQL using XAMPP
```

Open:

```
http://localhost/Smart-Vending-Machine
```

---

Future Improvements 

* Mobile application support
* QR code payments
* AI-based product recommendations
* Face recognition authentication
* IoT integration for real-time machine monitoring
* Cloud-based analytics dashboard



screenshots

(Add your project screenshots here)

Example:



Author 

Edson Lukiza, Joyce Wilfred, Joyce Wildad, Alice Msongole, Levina Chifie

GitHub: [https://github.com/Edsonlukiza](https://github.com/Edsonlukiza)


License 

This project is created for educational and development purposes.



